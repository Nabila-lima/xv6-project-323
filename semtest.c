// semtest.c — quick test of sem_init / sem_wait / sem_signal
#include "types.h"
#include "user.h"

int
main(void)
{
  int sem;
  int pid;

  printf(1, "\n=== SEMAPHORE TEST ===\n");

  // Start the semaphore at 0 -> child must wait until parent signals
  sem = sem_init(0);
  if(sem < 0){
    printf(1, "sem_init failed\n");
    exit();
  }
  printf(1, "Created semaphore with id %d (initial value 0)\n", sem);

  pid = fork();

  if(pid == 0){
    // Child: will block here until parent calls sem_signal
    printf(1, "Child: waiting on semaphore...\n");
    sem_wait(sem);
    printf(1, "Child: got the signal, proceeding!\n");
    exit();
  } else {
    // Parent: do a bit of "work", then release the child
    printf(1, "Parent: doing some work...\n");
    for(int i = 0; i < 1000000; i++)
      ;  // busy loop, simulates work
    printf(1, "Parent: signaling child now\n");
    sem_signal(sem);
    wait();
    printf(1, "Parent: child finished. Test complete.\n");
  }

  printf(1, "=======================\n\n");
  exit();
}