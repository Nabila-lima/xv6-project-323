```c
// rrtest.c
//
// Round Robin Scheduler Test
//
// Creates 3 CPU-bound processes.
// Each child continuously consumes CPU and periodically
// prints its PID.
//
// With a 10-tick quantum, the scheduler should rotate
// between runnable processes:
//
//      P1 -> P2 -> P3 -> P1 -> P2 -> P3 ...
//
// Use "ps" while this program is running to inspect:
// CPU ticks, context switches and preemptions.

#include "types.h"
#include "user.h"


int
main(void)
{
  int i;
  int pid;
  int child;


  printf(1, "\n");
  printf(1, "========================================\n");
  printf(1, "       ROUND ROBIN SCHEDULER TEST\n");
  printf(1, "========================================\n");
  printf(1, "Default quantum: 10 ticks\n");
  printf(1, "Creating 3 CPU-bound processes...\n");
  printf(1, "========================================\n\n");


  // Create three CPU-bound processes.
  for(i = 0; i < 3; i++){

    pid = fork();


    if(pid < 0){

      printf(1, "rrtest: fork failed\n");

      exit();
    }


    if(pid == 0){

      // Child process.
      child = i + 1;


      printf(1,
             "Child %d started: PID=%d\n",
             child,
             getpid());


      // CPU-bound loop.
      //
      // We deliberately do not sleep or yield here.
      // Therefore the timer interrupt must preempt us
      // when our Round Robin quantum expires.

      for(;;){

        volatile int x;

        x = 0;

        for(x = 0; x < 1000000; x++){

          // Consume CPU.
          x = x;
        }
      }
    }
  }


  // Parent stays alive so the user can run "ps"
  // and observe all three children.
  printf(1, "\n");
  printf(1, "3 CPU-bound processes created.\n");
  printf(1, "Run 'ps' in another shell/terminal if available.\n");
  printf(1, "Press Ctrl-P to inspect the process table.\n");
  printf(1, "The children will continuously consume CPU.\n\n");


  // Parent waits forever.
  for(;;){

    sleep(100);
  }


  exit();
}
```
