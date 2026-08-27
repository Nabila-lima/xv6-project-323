#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"


// ---------------------------------------------------------
// Process system calls
// ---------------------------------------------------------

int
sys_fork(void)
{
  return fork();
}


int
sys_exit(void)
{
  exit();

  // Not reached.
  return 0;
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


// ---------------------------------------------------------
// Process memory
// ---------------------------------------------------------

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


// ---------------------------------------------------------
// Sleep
// ---------------------------------------------------------

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


// ---------------------------------------------------------
// Uptime
// ---------------------------------------------------------

// Return how many clock tick interrupts have occurred
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


// ---------------------------------------------------------
// Get year
// ---------------------------------------------------------

int
sys_getyear(void)
{
  return 2025;
}


// ---------------------------------------------------------
// Round Robin: Set quantum
// ---------------------------------------------------------

int
sys_setquantum_pid(void)
{
  int pid;
  int quantum;

  if(argint(0, &pid) < 0)
    return -1;

  if(argint(1, &quantum) < 0)
    return -1;

  return setquantum_pid(pid, quantum);
}


// ---------------------------------------------------------
// Round Robin: Get quantum
// ---------------------------------------------------------

int
sys_gettimeslice(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;

  return gettimeslice(pid);
}


// ---------------------------------------------------------
// Get process information
// ---------------------------------------------------------

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


// ---------------------------------------------------------
// Get scheduling statistics
// ---------------------------------------------------------

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


// ---------------------------------------------------------
// Semaphores
// ---------------------------------------------------------

int
sys_sem_init(void)
{
  int value;

  if(argint(0, &value) < 0)
    return -1;

  return sem_init(value);
}


int
sys_sem_wait(void)
{
  int id;

  if(argint(0, &id) < 0)
    return -1;

  return sem_wait(id);
}


int
sys_sem_signal(void)
{
  int id;

  if(argint(0, &id) < 0)
    return -1;

  return sem_signal(id);
}
