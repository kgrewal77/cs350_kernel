#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>


/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */


int dirToInt(Direction d);
bool ableToEnter(int origin, int destination);
int mod(int a, int b);


static struct lock *logicLock;
static struct cv *logicCV;
int** currentlyIn;	
//Each direction corresponds to a number 0-3. N = 1, E = 0, S = 3, W = 2, based on degrees (0->360)/90
//so currentlyIn[0][1] indicates the number of cars in the intersection which are going N->E


//Basically, we will test all (o, d) pairs which cause collisions with the current car to determine
// if cars are present in the intersection at those pairs with currentlyIn[o][d]
//If we find one or more cars in this way, this car sleeps until a car exits the intersection and calls wakeone()
// otherwise it enters


/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */


int
mod(int a, int b){
  int c = a % b;
  if (c < 0){
    c = b+c;
  }
  return c;
}


void
intersection_sync_init(void)
{
  
  currentlyIn= kmalloc(4 * sizeof(int*));
  if (currentlyIn == NULL) {
    panic("could not create current array");
  }

  logicLock = lock_create("logicLock");
  if (logicLock == NULL) {
    panic("could not create logic lock");
  }

  logicCV = cv_create("logicCV");
  if (logicCV == NULL) {
    panic("could not create logic CV");
  }

  for (int i = 0;i<4;i++){
    currentlyIn[i] = kmalloc(4 * sizeof(int));
    if (currentlyIn[i] == NULL) {
      panic("could not create current array");
    }

    for (int j = 0;j<4;j++){
    currentlyIn[i][j] = 0;
    }
  }

  
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  for (int i=0;i<4;i++){
    kfree(currentlyIn[i]);
  }
  kfree(currentlyIn);
  cv_destroy(logicCV);
  lock_destroy(logicLock);
}


int
dirToInt(Direction d){
  switch (d)
    {
    case north:
      return 1;
    case east:
      return 0;
    case south:
      return 3;
    case west:
      return 2;
    }
  panic("Direction of non-specified type");
  return 42;
}

bool
ableToEnter(int origin, int destination){
  lock_release(logicLock);
  int conflictTable[7][2] = {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}};
  int size = 0;
  int j = destination;
  int i = origin;
  switch(mod((j-i), 4)){
    //invalid
    case 0 :
      return false;
    //Right
    case 1 :
      //(Ahead->Right),(Left->Right)
      conflictTable[0][0] = mod((i-2), 4);
      conflictTable[0][1] = j;
      conflictTable[1][0] = mod((i-1), 4);
      conflictTable[1][1] = j;
      size = 2;
      break;
    //straight 
    case 2 :
      //(Ahead->Right),(Right->Origin),(Right->Left),(Right->Ahead),(Left->Right),(Left->Ahead)
      conflictTable[0][0] = j;
      conflictTable[0][1] = mod((i+1), 4);
      conflictTable[1][0] = mod((i+1), 4);
      conflictTable[1][1] = i;
      conflictTable[2][0] = mod((i+1), 4);
      conflictTable[2][1] = mod((i-1), 4);
      conflictTable[3][0] = mod((i+1), 4);
      conflictTable[3][1] = j;
      conflictTable[4][0] = mod((i-1), 4);
      conflictTable[4][1] = mod((i+1), 4);
      conflictTable[5][0] = mod((i-1), 4);
      conflictTable[5][1] = j;
      size = 6;
      break;
    //left
    case 3 :
      //(Ahead->Right),(Ahead->Left),(Ahead->Origin),(Right->Origin),(Right->Left),(Left->Right),(Left->Ahead)
      conflictTable[0][0] = mod((i-2), 4);
      conflictTable[0][1] = mod((i+1), 4);
      conflictTable[1][0] = mod((i-2), 4);
      conflictTable[1][1] = j;
      conflictTable[2][0] = mod((i-2), 4);
      conflictTable[2][1] = i;
      conflictTable[3][0] = mod((i+1), 4);
      conflictTable[3][1] = i;
      conflictTable[4][0] = mod((i+1), 4);
      conflictTable[4][1] = j;
      conflictTable[5][0] = j;
      conflictTable[5][1] = mod((i+1), 4);
      conflictTable[6][0] = j;
      conflictTable[6][1] = mod((i-2), 4);
      size = 7;
      break;
  }
      
  lock_acquire(logicLock);
  for (int k = 0;k<size;k++){
    int x = conflictTable[k][0];
    int y = conflictTable[k][1];
    if (currentlyIn[x][y] != 0){
      return false;
    }
  }
  
  return true;
}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  KASSERT(logicLock != NULL);
  KASSERT(logicCV != NULL);
  lock_acquire(logicLock);
  int o = dirToInt(origin);
  int d = dirToInt(destination);
  while (!(ableToEnter(o,d))){
    cv_wait(logicCV, logicLock);
  }
  currentlyIn[o][d] = currentlyIn[o][d] + 1;
  lock_release(logicLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  KASSERT(logicLock != NULL);
  KASSERT(logicCV != NULL);
  lock_acquire(logicLock);
  int o = dirToInt(origin);
  int d = dirToInt(destination);
  currentlyIn[o][d] = currentlyIn[o][d] - 1;
  cv_signal(logicCV, logicLock);
  lock_release(logicLock);

}
