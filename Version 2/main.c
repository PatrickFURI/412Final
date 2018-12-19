//
//  main.c
//  Final Project CSC412
//
//  Created by Jean-Yves Hervé on 2018-12-05
//	This is public domain code.  By all means appropriate it and change is to your
//	heart's content.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>

#include "gl_frontEnd.h"

//==================================================================================
//	Function prototypes
//==================================================================================
void displayGridPane(void);
void displayStatePane(void);
void initializeApplication(void);


//==================================================================================
//	Application-level global variables
//==================================================================================

//	Don't touch
extern const int	GRID_PANE, STATE_PANE;
extern int	gMainWindow, gSubwindow[2];

//	Don't rename any of these variables
//-------------------------------------
//	The state grid and its dimensions (arguments to the program)
int** grid;
int numRows = -1;	//	height of the grid
int numCols = -1;	//	width
int numBoxes = -1;	//	also the number of robots
int numDoors = -1;	//	The number of doors.

int numLiveThreads = 0;		//	the number of live robot threads

//	robot sleep time between moves (in microseconds)
const int MIN_SLEEP_TIME = 1000;
int robotSleepTime = 100000;

//	An array of C-string where you can store things you want displayed
//	in the state pane to display (for debugging purposes?)
//	Dont change the dimensions as this may break the front end
const int MAX_NUM_MESSAGES = 8;
const int MAX_LENGTH_MESSAGE = 32;
char** message;

//location arrays
int **robotLoc = NULL;
int **boxLoc = NULL;
int *doorAssign = NULL;
int **doorLoc = NULL;

//output file pointer
FILE *fp;
pthread_mutex_t *fpLock;

void printoutPositionInfo() {
	pthread_mutex_lock(fpLock);
	// printout door positions
	for (int i = 0; i < numDoors; i++) {
		fprintf(fp, "Door %d: %d x %d (r x c)\n", i, doorLoc[i][0], doorLoc[i][1]);
	}
	fprintf(fp, "\n");
	
	// printout box information
	for (int i = 0; i < numBoxes; i++) {
		fprintf(fp, "Box %d: %d x %d (r x c)\n", i, boxLoc[i][0], boxLoc[i][1]); 
	}
	fprintf(fp, "\n");

	// printout robot information (with its destination door)
	for (int i = 0; i < numBoxes; i++) {
		fprintf(fp, "Robot %d: %d x %d (r x c) at desination door: %d\n", 
				i, robotLoc[i][0], robotLoc[i][1], doorAssign[i]); 
	}
        fprintf(fp, "\n");
	pthread_mutex_unlock(fpLock);
}

/*! \brief Test if position is in array.
\param curArray Array currently being populated.
\param index Index to test in current array.
\param array Array to search.
\return True if pos is in array[0:count], otherwise false.
*/
bool inArray(int **curArray, int index, int **array)
{
	if(array == NULL)
		return false;
	int *pos = curArray[index];
	int maxIndex = curArray == array ? index : numBoxes;
	for(int i = 0; i < maxIndex; i++)
		if(pos[0] == array[i][0] && pos[1] == array[i][1])
			return true;
	return false;
}

/*! \brief Populate array with unique random positions.
\param count Number of positions to create
\param arrayPtr Pointer to array to populate.
*/
void randomArray(int count, int ***arrayPtr)
{
	//initialize memory
	*arrayPtr = malloc(count * sizeof(int *));
	int **array = *arrayPtr;
	for(int i = 0; i < count; i++)
	{
		array[i] = malloc(2 * sizeof(int));
		do
		{
			array[i][0] = (rand() % (numCols - 2)) + 1;
			array[i][1] = (rand() % (numRows - 2)) + 1;
		} while(inArray(array, i, robotLoc) || inArray(array, i, boxLoc) ||inArray(array, i, doorLoc));
	}
}

/*! \brief Randomly generate positions for all objects.*/
void randomlyGeneratePositions() {
	// assign the door ids to each robot-box pair
	doorAssign = (int *) malloc(numBoxes * sizeof(int));
	for (int i=0; i<numBoxes; i++) {
		int doorID = rand() % numDoors;
		doorAssign[i] = doorID;
	}

	//create randomized arrays
	randomArray(numBoxes, &robotLoc);
	randomArray(numBoxes, &boxLoc);
	randomArray(numBoxes, &doorLoc);
}

/*! \brief Moves robot to coordinates.
\param robot Robot number.
\param dir Direction to move in, either 'N', 'S', 'E', or 'W'.
*/
void moveRobot(unsigned robot, char dir)
{
        bool boxPushed = false; 
	switch(dir)
	{
		case 'N':
			robotLoc[robot][1]++;
			if(robotLoc[robot][0] == boxLoc[robot][0] && robotLoc[robot][1] == boxLoc[robot][1]) {
				boxLoc[robot][1]++;
			        boxPushed = true;
			}
			break;
		case 'S':
			robotLoc[robot][1]--;
	                if(robotLoc[robot][0] == boxLoc[robot][0] && robotLoc[robot][1] == boxLoc[robot][1]) {
				boxLoc[robot][1]--;
				boxPushed = true; 
			}
			break;
		case 'E':
			robotLoc[robot][0]--;
			if(robotLoc[robot][0] == boxLoc[robot][0] && robotLoc[robot][1] == boxLoc[robot][1]) {
				boxLoc[robot][0]--;
				boxPushed = true; 
			}
			break;
		case 'W':
			robotLoc[robot][0]++;
			if(robotLoc[robot][0] == boxLoc[robot][0] && robotLoc[robot][1] == boxLoc[robot][1]) {
				boxLoc[robot][0]++;
				boxPushed = true; 
			}
			break;
	}
	usleep(robotSleepTime);
	pthread_mutex_lock(fpLock);
	if (boxPushed) {
	       fprintf(fp, "%s %d %s %c\n", "robot", robot, "push", dir);
	} else {
	       fprintf(fp, "%s %d %s %c\n", "robot", robot, "move", dir);
	}
	pthread_mutex_unlock(fpLock);
}

/*! \brief Solves problem for robot.
\param arg Pointer to robot number to solve for.
\return NULL
*/
void *solveRobot(void *arg)
{
	unsigned robot = *((unsigned *)arg);
	//find amount robot should move to reach box
	int dxRob=boxLoc[robot][0] - robotLoc[robot][0];
	int dyRob=boxLoc[robot][1] - robotLoc[robot][1];
	//find amount box should move to reach door
	int dxBox=doorLoc[doorAssign[robot]][0] - boxLoc[robot][0];
	int dyBox=doorLoc[doorAssign[robot]][1] - boxLoc[robot][1];

	//adjust robot goal position for box movement direction
	if(dxBox > 0)
		dxRob--;
	else if(dxBox < 0)
		dxRob++;
	else
		if(dyBox > 0)
			dyRob--;
		else if(dyBox < 0)
			dyRob++;
		//problem was solved by other robot since box is on door
		else
			return NULL;
	
	//go to correct x position for robot
	while(dxRob != 0)
	{
		if(dxRob > 0)
		{
			moveRobot(robot, 'W');
			dxRob--;
		}
		else
		{
			moveRobot(robot, 'E');
			dxRob++;
		}
		if(dxRob == 2 && dyRob == 0 && boxLoc[robot][0] - robotLoc[robot][0] == 1)
		{
			moveRobot(robot, 'N');
			moveRobot(robot, 'W');
			moveRobot(robot, 'W');
			moveRobot(robot, 'S');
			dxRob = 0;
			break;
		}
		if(dxRob == -2 && dyRob == 0 && boxLoc[robot][0] - robotLoc[robot][0] == -1)
		{
			moveRobot(robot, 'S');
			moveRobot(robot, 'E');
			moveRobot(robot, 'E');
			moveRobot(robot, 'N');
			dxRob = 0;
			break;
		}
	}
	//go to correct y position for robot
	while(dyRob != 0)
	{
		if(dyRob > 0)
		{
			moveRobot(robot, 'N');
			dyRob--;
		}
		else
		{
			moveRobot(robot, 'S');
			dyRob++;
		}
		if(dyRob == 2 && boxLoc[robot][1] - robotLoc[robot][1] == 1)
		{
			moveRobot(robot, 'E');
			moveRobot(robot, 'N');
			moveRobot(robot, 'N');
			moveRobot(robot, 'W');
			dxRob = 0;
			break;
		}
		if(dyRob == -2 && boxLoc[robot][1] - robotLoc[robot][1] == -1)
		{
			moveRobot(robot, 'W');
			moveRobot(robot, 'S');
			moveRobot(robot, 'S');
			moveRobot(robot, 'E');
			dxRob = 0;
			break;
		}
	}
	//go to correct x position for box
	while(dxBox != 0)
	{
		if(dxBox > 0)
		{
			moveRobot(robot, 'W');
			dxBox--;
			//go to correct position to push
			if(dxBox == 0 && dyBox != 0)
			{
				if(dyBox > 0)
					moveRobot(robot, 'S');
				else if(dyBox < 0)
					moveRobot(robot, 'N');
				moveRobot(robot, 'W');
			}
		}
		else
		{
			moveRobot(robot, 'E');
			dxBox++;
			//go to correct position to push
			if(dxBox == 0 && dyBox != 0)
			{
				if(dyBox > 0)
					moveRobot(robot, 'S');
				else if(dyBox < 0)
					moveRobot(robot, 'N');
				moveRobot(robot, 'E');
			}
		}
	}
	//go to correct y position for box
	while(dyBox != 0)
	{
		if(dyBox > 0)
		{
			moveRobot(robot, 'N');
			dyBox--;
		}
		else
		{
			moveRobot(robot, 'S');
			dyBox++;
		}
	}
	//remove completed box and robot
	boxLoc[robot][0] = -1;
	boxLoc[robot][0] = -1;
	robotLoc[robot][1] = -1;
	robotLoc[robot][1] = -1;
	
	free(arg);
	return NULL;
}

/*! \brief Function for main logic thread to run.
\param arg NULL
\return NULL
*/
void *mainThreadFunc(void *arg)
{
	for(unsigned i = 0; i < (unsigned)numBoxes; i++)
	{
		//start threads
		pthread_t thread;
		unsigned *temp = malloc(sizeof(unsigned));
		*temp = i;
		int errCode = pthread_create(&thread, NULL, solveRobot, temp);
		if (errCode != 0)
		{
			printf ("could not pthread_create main thread. Error code %d: %s\n",
				 errCode, strerror(errCode));
			exit (13);
		}
	}

	return NULL;
}

//==================================================================================
//	These are the functions that tie the simulation with the rendering.
//	Some parts are "don't touch."  Other parts need your intervention
//	to make sure that access to critical section is properly synchronized
//==================================================================================

void displayGridPane(void)
{
	//	This is OpenGL/glut magic.  Don't touch
	glutSetWindow(gSubwindow[GRID_PANE]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	for (int i=0; i<numBoxes; i++)
	{
		//	here I would test if the robot thread is still live
		drawRobotAndBox(i, robotLoc[i][1], robotLoc[i][0], boxLoc[i][1], boxLoc[i][0], doorAssign[i]);
	}

	for (int i=0; i<numDoors; i++)
	{
		//	here I would test if the robot thread is still alive
		drawDoor(i, doorLoc[i][1], doorLoc[i][0]);
	}

	//	This call does nothing important. It only draws lines
	//	There is nothing to synchronize here
	drawGrid();

	//	This is OpenGL/glut magic.  Don't touch
	glutSwapBuffers();
	
	glutSetWindow(gMainWindow);
}

void displayStatePane(void)
{
	//	This is OpenGL/glut magic.  Don't touch
	glutSetWindow(gSubwindow[STATE_PANE]);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	//	Here I hard-code a few messages that I want to see displayed
	//	in my state pane.  The number of live robot threads will
	//	always get displayed.  No need to pass a message about it.
	int numMessages = 3;
	sprintf(message[0], "We have %d doors", numDoors);
	sprintf(message[1], "I like cheese");
	sprintf(message[2], "System time is %ld", time(NULL));
	
	//---------------------------------------------------------
	//	This is the call that makes OpenGL render information
	//	about the state of the simulation.
	//
	//	You *must* synchronize this call.
	//
	//---------------------------------------------------------
	drawState(numMessages, message);
	
	
	//	This is OpenGL/glut magic.  Don't touch
	glutSwapBuffers();
	
	glutSetWindow(gMainWindow);
}

//------------------------------------------------------------------------
//	You shouldn't have to touch this one.  Definitely if you don't
//	add the "producer" threads, and probably not even if you do.
//------------------------------------------------------------------------
void speedupRobots(void)
{
	//	decrease sleep time by 20%, but don't get too small
	int newSleepTime = (8 * robotSleepTime) / 10;
	
	if (newSleepTime > MIN_SLEEP_TIME)
	{
		robotSleepTime = newSleepTime;
	}
}

void slowdownRobots(void)
{
	//	increase sleep time by 20%
	robotSleepTime = (12 * robotSleepTime) / 10;
}

//------------------------------------------------------------------------
//	You shouldn't have to change anything in the main function besides
//	the initialization of numRows, numCos, numDoors, numBoxes.
//------------------------------------------------------------------------
int main(int argc, char** argv)
{
	//Read arguments
	if(argc != 5)
	{
		fprintf(stderr, "Program requires 4 arguments\n%s rows cols boxes doors\n", argv[0]);
		return 1;
	}
	if(sscanf(argv[1], "%d", &numRows) != 1 ||
		sscanf(argv[2], "%d", &numCols) != 1 ||
		sscanf(argv[3], "%d", &numBoxes) != 1 ||
		sscanf(argv[4], "%d", &numDoors) != 1)
	{
		fprintf(stderr, "Error parsing arguments\n");
		return 1;
	}
	if(numDoors > 3 || numDoors < 1)
	{
		fprintf(stderr, "Program supports only 1 to 3 doors\n");
		return 1;
	}
	fpLock = calloc(1, sizeof(pthread_mutex_t));
	pthread_mutex_init(fpLock, NULL);
	fp = fopen("robotSimulOut.txt", "w");

	//	Even though we extracted the relevant information from the argument
	//	list, I still need to pass argc and argv to the front-end init
	//	function because that function passes them to glutInit, the required call
	//	to the initialization of the glut library.
	initializeFrontEnd(argc, argv, displayGridPane, displayStatePane);
	
	//	Now we can do application-level initialization
	initializeApplication();

	//	Now we enter the main loop of the program and to a large extend
	//	"lose control" over its execution.  The callback functions that 
	//	we set up earlier will be called when the corresponding event
	//	occurs
	glutMainLoop();
	
	//	Free allocated resource before leaving (not absolutely needed, but
	//	just nicer.  Also, if you crash there, you know something is wrong
	//	in your code.
	for (int i=0; i< numRows; i++)
		free(grid[i]);
	free(grid);
	for (int k=0; k<MAX_NUM_MESSAGES; k++)
		free(message[k]);
	free(message);

	
	//	This will probably never be executed (the exit point will be in one of the
	//	call back functions).
	return 0;
}


//==================================================================================
//
//	This is a part that you have to edit and add to.
//
//==================================================================================


void initializeApplication(void)
{
	//	Allocate the grid
	grid = (int**) malloc(numRows * sizeof(int*));
	for (int i=0; i<numRows; i++)
		grid[i] = (int*) malloc(numCols * sizeof(int));
	
	message = (char**) malloc(MAX_NUM_MESSAGES*sizeof(char*));
	for (int k=0; k<MAX_NUM_MESSAGES; k++)
		message[k] = (char*) malloc((MAX_LENGTH_MESSAGE+1)*sizeof(char));
		
	//---------------------------------------------------------------
	//	All the code below to be replaced/removed
	//	I initialize the grid's pixels to have something to look at
	//---------------------------------------------------------------
	//	Yes, I am using the C random generator after ranting in class that the C random
	//	generator was junk.  Here I am not using it to produce "serious" data (as in a
	//	simulation), only some color, in meant-to-be-thrown-away code
	
	//	seed the pseudo-random generator
	srand((unsigned int) time(NULL));

	//	normally, here I would initialize the location of my doors, boxes,
	//	and robots, and create threads (not necessarily in that order).
	//	For the handout I have nothing to do.
	randomlyGeneratePositions();

	//start main thread
	pthread_t mainThread;
	int errCode = pthread_create(&mainThread, NULL, mainThreadFunc, NULL);
	if (errCode != 0)
	{
		printf ("could not pthread_create main thread. Error code %d: %s\n",
			 errCode, strerror(errCode));
		exit (13);
	}
}
