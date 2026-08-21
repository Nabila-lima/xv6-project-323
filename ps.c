// ps.c — Process Monitor
// Prints a table of all active processes: PID, name, state,
// priority, timeslice, and scheduling statistics.
#include "types.h"
#include "user.h"



char *statename[] = {
  "unused", "embryo", "sleep ", "runble", "run   ", "zombie"
};

int
main(void)
{
  struct procinfo pi;
  int found = 0;

  printf(1, "\n=== PROCESS MONITOR ===\n");
  printf(1, "PID\tNAME\t\tSTATE\tPRIO\tQUANTUM\tCPU\tSLEEP\tCTXSW\tPREEMPT\n");
  printf(1, "----------------------------------------------------------------------------\n");

  for(int pid = 1; pid <= 64; pid++){
    if(getprocinfo(pid, &pi) >= 0){
      found = 1;
      char *st = (pi.state >= 0 && pi.state <= 5) ? statename[pi.state] : "?     ";
      printf(1, "%d\t%s\t\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n",
             pi.pid, pi.name, st, pi.priority, pi.timeslice,
             pi.cpu_ticks, pi.sleep_ticks, pi.ctx_switches, pi.preemptions);
    }
  }

  if(!found)
    printf(1, "No processes found.\n");

  printf(1, "----------------------------------------------------------------------------\n\n");
  exit();
}