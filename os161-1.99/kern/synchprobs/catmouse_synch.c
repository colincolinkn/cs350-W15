#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct semaphore *globalCatMouseSem;

static struct cv *cv_queue; /* waiting creatures */
static struct lock *lk_bowl; /* lock to bowl status */

static int Num_Bowls; /* length of the bowl list */

static volatile int Num_Cat;  /* num of waiting cats*/
static volatile int Num_Mouse; /* num of waiting mice*/
static volatile char *bowl_status; /* status in bowl list */
/*
 * index 0: present current status of the whole list. 
 *          (Cats are eating or Mice are eating)
 * c: this bowl is occupied by a cat
 * m: this bowl is occupied by a mouse
 * -: this bowl is not occupied
 */

/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_init */

  (void)bowls; /* keep the compiler from complaining about unused parameters */
  globalCatMouseSem = sem_create("globalCatMouseSem",1);
  if (globalCatMouseSem == NULL) {
    panic("could not create global CatMouse synchronization semaphore");
  }

  Num_Bowls = bowls;
  int i;
  bowl_status = kmalloc((bowls+1)*sizeof(char));
  for (i=0;i<=bowls;i++){
     bowl_status[i] = '-';
  }
	
  cv_queue = cv_create("cvQueue");
  if (cv_queue == NULL) {
	panic("could not creat cv queue");
  }

  lk_bowl = lock_create("bowlLock");
  if (lk_bowl == NULL) {
    panic("could not create bowl lock");
  }

  Num_Cat = 0;
  Num_Mouse = 0;  
  return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_cleanup */
  (void)bowls; /* keep the compiler from complaining about unused parameters */
  KASSERT(globalCatMouseSem != NULL);
  sem_destroy(globalCatMouseSem);

  KASSERT(lk_bowl != NULL);
  lock_destroy(lk_bowl);

  KASSERT(cv_queue != NULL);
  cv_destroy(cv_queue);

}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
  /* replace this default imp lementation with your own implementation of cat_before_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  lock_acquire(lk_bowl);
  Num_Cat++;
  if (Num_Cat+Num_Mouse == 1) {
     bowl_status[0] = 'c';
  } else {
     while(bowl_status[(int)bowl] != '-' || bowl_status[0] != 'c' ) {
        cv_wait(cv_queue,lk_bowl);
     }
  }
  bowl_status[(int)bowl] = 'c';
  lock_release(lk_bowl);
  
 //KASSERT(globalCatMouseSem != NULL);
 // P(globalCatMouseSem);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_after_eating */
  
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  lock_acquire(lk_bowl);
  bowl_status[(int)bowl] = '-';
  for (int i = 1; i <= Num_Bowls; i++) {
        KASSERT(bowl_status[i] != 'm');
        if (bowl_status[i] == 'c') {
                bowl_status[0] = 'c';
                break;
        } 
        if (Num_Mouse > 0) { bowl_status[0] = 'm';}
  }
  Num_Cat--;
  lock_release(lk_bowl);
  cv_broadcast(cv_queue, lk_bowl);

  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
  //V(globalCatMouseSem);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_before_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  lock_acquire(lk_bowl);
  Num_Mouse++;
  if (Num_Mouse+Num_Cat == 1){
      bowl_status[0] = 'm';
  } else {
  while(bowl_status[(int)bowl] != '-' || bowl_status[0] != 'm' ) {
     cv_wait(cv_queue,lk_bowl);
  }
  }
  bowl_status[(int)bowl] = 'm';
  lock_release(lk_bowl);
  //KASSERT(globalCatMouseSem != NULL);
  //P(globalCatMouseSem);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_after_eating */
  (void)bowl;  /* keep the compiler from complaining about an unused parameter */
  lock_acquire(lk_bowl);
  bowl_status[(int)bowl] = '-';
  for (int i = 1; i <= Num_Bowls; i++) {
        KASSERT(bowl_status[i] != 'c');
        if (bowl_status[i] == 'm') {
                bowl_status[0] = 'm';
                break;
        } 
        if (Num_Cat>0) { bowl_status[0] = 'c';}
  }
  Num_Mouse--;
  lock_release(lk_bowl);
  cv_broadcast(cv_queue, lk_bowl);
  //KASSERT(globalCatMouseSem != NULL);
  //V(globalCatMouseSem);
}
