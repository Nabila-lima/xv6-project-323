#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

// These are declared in proc.c — we just need to use them
extern struct proc proc[NPROC];
extern struct spinlock ptable_lock;
// In sysproc.c, at the top after includes:
extern int getpstats(int pid, struct pstats *ps);

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_getyear(void)
{
  return 2025;  // Simple hardcoded year
}

int
sys_setquantum_pid(void)
{
  int pid, quantum;
  if(argint(0, &pid) < 0 || argint(1, &quantum) < 0)
    return -1;
  return setquantum_pid(pid, quantum);
}
int
sys_getprocinfo(void)
{
  int pid;
  struct procinfo *pi;

  if(argint(0, &pid) < 0)
    return -1;
  if(argptr(1, (void*)&pi, sizeof(*pi)) < 0)
    return -1;

  return getprocinfo(pid, pi);
}
int
sys_gettimeslice(void)
{
  int pid;
  if(argint(0, &pid) < 0)
    return -1;
  return gettimeslice(pid);
}

int
sys_getpstats(void)
{
  int pid;
  struct pstats *ps;

  if(argint(0, &pid) < 0)
    return -1;
  if(argptr(1, (void*)&ps, sizeof(*ps)) < 0)
    return -1;

  return getpstats(pid, ps);
}
