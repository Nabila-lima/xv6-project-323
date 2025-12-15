#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    printf(2, "Usage: pstats <pid>\n");
    exit();
  }

  int pid = atoi(argv[1]);
  struct pstats ps;

  if(getpstats(pid, &ps) < 0){
    printf(2, "Failed to get stats for pid %d\n", pid);
    exit();
  }

  printf(1, "PID: %d\n", pid);
  printf(1, "CPU ticks      : %d\n", ps.cpu_ticks);
  printf(1, "Sleep ticks    : %d\n", ps.sleep_ticks);
  printf(1, "Runnable ticks : %d\n", ps.runnable_ticks);
  printf(1, "Context switch : %d\n", ps.ctx_switches);
  printf(1, "Preemptions    : %d\n", ps.preemptions);

int total = ps.cpu_ticks + ps.sleep_ticks + ps.runnable_ticks;
if(total == 0) total = 1;

int cpu_share = (ps.cpu_ticks * 100) / total;
int wait_ratio = (ps.cpu_ticks == 0) ? 0 :
                 (ps.runnable_ticks * 100) / ps.cpu_ticks;
int preempt_ratio = (ps.ctx_switches == 0) ? 0 :
                    (ps.preemptions * 100) / ps.ctx_switches;

printf(1,
"Fairness:\n CPU-share: %d%%\n Wait-ratio: %d%%\n Preempt-ratio: %d%%\n",
cpu_share, wait_ratio, preempt_ratio);



  exit();
}
