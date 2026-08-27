#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define DEFAULT_QUANTUM 10

// Process table
struct {
  struct spinlock lock;
  struct proc proc[NPROC];

  // Round Robin:
  // index where the next scheduler search begins.
  int rr_next;
} ptable;

static struct proc *initproc;

#define NSEM 10

struct spinlock semlock;

struct semaphore {
  int value;
  int allocated;
} semtable[NSEM];

int nextpid = 1;

extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);


// ---------------------------------------------------------
// Initialize process table
// ---------------------------------------------------------

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&semlock, "semlock");

  // Round Robin starts from the first process.
  ptable.rr_next = 0;
}


// ---------------------------------------------------------
// Return current CPU ID
// ---------------------------------------------------------

// Must be called with interrupts disabled
int
cpuid()
{
  return mycpu()-cpus;
}


// ---------------------------------------------------------
// Return current CPU
// ---------------------------------------------------------

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();

  // APIC IDs are not guaranteed to be contiguous.
  for(i = 0; i < ncpu; ++i){
    if(cpus[i].apicid == apicid)
      return &cpus[i];
  }

  panic("unknown apicid\n");
  return 0;
}


// ---------------------------------------------------------
// Return current process
// ---------------------------------------------------------

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure.
struct proc*
myproc(void)
{
  struct cpu *c;
  struct proc *p;

  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();

  return p;
}


// ---------------------------------------------------------
// Allocate a process
// ---------------------------------------------------------

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // -------------------------------------------------------
  // Round Robin scheduling initialization
  // -------------------------------------------------------

  p->timeslice = DEFAULT_QUANTUM;
  p->ticks_used = 0;

  // Clear scheduling statistics.
  memset(&p->stats, 0, sizeof(p->stats));

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }

  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}


// ---------------------------------------------------------
// First user process
// ---------------------------------------------------------

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;

  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");

  inituvm(p->pgdir, _binary_initcode_start,
          (int)_binary_initcode_size);

  p->sz = PGSIZE;

  memset(p->tf, 0, sizeof(*p->tf));

  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;

  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // Make the process runnable.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}


// ---------------------------------------------------------
// Grow current process memory
// ---------------------------------------------------------

int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  curproc->sz = sz;
  switchuvm(curproc);

  return 0;
}


// ---------------------------------------------------------
// Fork
// ---------------------------------------------------------

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);

  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  // Every new process starts with the default
  // Round Robin quantum.
  np->timeslice = DEFAULT_QUANTUM;
  np->ticks_used = 0;

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}


// ---------------------------------------------------------
// Exit
// ---------------------------------------------------------

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();

  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;

      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();

  panic("zombie exit");
}


// ---------------------------------------------------------
// Wait
// ---------------------------------------------------------

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);

  for(;;){

    // Scan through table looking for exited children.
    havekids = 0;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

      if(p->parent != curproc)
        continue;

      havekids = 1;

      if(p->state == ZOMBIE){

        // Found one.
        pid = p->pid;

        kfree(p->kstack);
        p->kstack = 0;

        freevm(p->pgdir);

        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;

        p->state = UNUSED;

        release(&ptable.lock);

        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.
    sleep(curproc, &ptable.lock);
  }
}


// ---------------------------------------------------------
// Round Robin Scheduler
// ---------------------------------------------------------

// Per-CPU process scheduler.
//
// Round Robin scheduling:
//
//   1. Find a RUNNABLE process.
//   2. Start searching from rr_next.
//   3. Run that process.
//   4. Timer interrupt counts its CPU ticks.
//   5. When the fixed quantum expires,
//      trap.c calls yield().
//   6. yield() changes the process to RUNNABLE.
//   7. scheduler() selects the next process.
//
// Example:
//
//   P1 -> P2 -> P3 -> P1 -> P2 -> P3
//
// Each process receives the same fixed time quantum.

void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;

  for(;;){

    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);

    p = 0;

    // -----------------------------------------------------
    // Search process table in circular Round Robin order.
    // -----------------------------------------------------

    for(int i = 0; i < NPROC; i++){

      int index = (ptable.rr_next + i) % NPROC;
      struct proc *candidate = &ptable.proc[index];

      if(candidate->state == RUNNABLE){

        p = candidate;

        // Next search starts after this process.
        ptable.rr_next = (index + 1) % NPROC;

        break;
      }
    }

    if(p != 0){

      // Start a fresh CPU quantum.
      p->ticks_used = 0;

      // Process starts running.
      c->proc = p;
      p->state = RUNNING;

      // Count context switch.
      p->stats.ctx_switches++;

      switchuvm(p);

      // Switch from scheduler to process.
      swtch(&(c->scheduler), p->context);

      // Process has returned to scheduler.
      switchkvm();

      c->proc = 0;
    }

    release(&ptable.lock);
  }
}


// ---------------------------------------------------------
// Enter scheduler
// ---------------------------------------------------------

// Enter scheduler. Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");

  if(mycpu()->ncli != 1)
    panic("sched locks");

  if(p->state == RUNNING)
    panic("sched running");

  if(readeflags()&FL_IF)
    panic("sched interruptible");

  intena = mycpu()->intena;

  swtch(&p->context, mycpu()->scheduler);

  mycpu()->intena = intena;
}


// ---------------------------------------------------------
// Yield CPU
// ---------------------------------------------------------

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);

  // Return process to RUNNABLE state.
  myproc()->state = RUNNABLE;

  sched();

  release(&ptable.lock);
}


// ---------------------------------------------------------
// Fork return
// ---------------------------------------------------------

// A fork child's very first scheduling by scheduler()
// will swtch here. "Return" to "caller", actually trapret.
void
forkret(void)
{
  static int first = 1;

  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if(first){
    // Some initialization functions must be run in the
    // context of a regular process.
    first = 0;

    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
}


// ---------------------------------------------------------
// Sleep
// ---------------------------------------------------------

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to change p->state
  // and then call sched.
  if(lk != &ptable.lock){
    acquire(&ptable.lock);
    release(lk);
  }

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  // Record sleeping activity.
  p->stats.sleep_ticks++;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){
    release(&ptable.lock);
    acquire(lk);
  }
}


// ---------------------------------------------------------
// Wakeup
// ---------------------------------------------------------

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->state == SLEEPING && p->chan == chan){

      // No adaptive scheduling.
      // Simply make the process runnable.
      p->state = RUNNABLE;
    }
  }
}


// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);

  wakeup1(chan);

  release(&ptable.lock);
}


// ---------------------------------------------------------
// Kill
// ---------------------------------------------------------

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->pid == pid){

      p->killed = 1;

      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;

      release(&ptable.lock);

      return 0;
    }
  }

  release(&ptable.lock);

  return -1;
}


// ---------------------------------------------------------
// Process dump
// ---------------------------------------------------------

// Print a process listing to console. For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
    [UNUSED]    "unused",
    [EMBRYO]    "embryo",
    [SLEEPING]  "sleep ",
    [RUNNABLE]  "runble",
    [RUNNING]   "run   ",
    [ZOMBIE]    "zombie"
  };

  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->state == UNUSED)
      continue;

    if(p->state >= 0 &&
       p->state < NELEM(states) &&
       states[p->state])

      state = states[p->state];

    else
      state = "???";

    cprintf("%d %s %s",
            p->pid,
            state,
            p->name);

    if(p->state == SLEEPING){

      getcallerpcs((uint*)p->context->ebp + 2, pc);

      for(i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }

    cprintf("\n");
  }
}


// ---------------------------------------------------------
// Set process quantum
// ---------------------------------------------------------

// Set the Round Robin time quantum for a process.
//
// This function is optional. It allows the user to change
// the quantum of an individual process.
//
// For standard Round Robin testing, all processes normally
// use DEFAULT_QUANTUM = 10.

int
setquantum_pid(int pid, int quantum)
{
  struct proc *p;

  if(quantum < 1 || quantum > 100)
    return -1;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->pid == pid && p->state != UNUSED){

      p->timeslice = quantum;

      // Restart the current quantum.
      p->ticks_used = 0;

      release(&ptable.lock);

      return 0;
    }
  }

  release(&ptable.lock);

  return -1;
}


// ---------------------------------------------------------
// Get process quantum
// ---------------------------------------------------------

int
gettimeslice(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->pid == pid && p->state != UNUSED){

      int ts = p->timeslice;

      release(&ptable.lock);

      return ts;
    }
  }

  release(&ptable.lock);

  return -1;
}


// ---------------------------------------------------------
// Get scheduling statistics
// ---------------------------------------------------------

int
getpstats(int pid, struct pstats *out)
{
  struct proc *p;

  if(out == 0)
    return -1;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->pid == pid && p->state != UNUSED){

      *out = p->stats;

      release(&ptable.lock);

      return 0;
    }
  }

  release(&ptable.lock);

  return -1;
}


// ---------------------------------------------------------
// Get process information
// ---------------------------------------------------------

int
getprocinfo(int pid, struct procinfo *out)
{
  struct proc *p;

  if(out == 0)
    return -1;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->pid == pid && p->state != UNUSED){

      out->pid          = p->pid;
      safestrcpy(out->name, p->name, sizeof(out->name));
      out->state        = p->state;

      // Round Robin information.
      out->timeslice    = p->timeslice;

      // Scheduling statistics.
      out->cpu_ticks    = p->stats.cpu_ticks;
      out->sleep_ticks  = p->stats.sleep_ticks;
      out->ctx_switches = p->stats.ctx_switches;
      out->preemptions  = p->stats.preemptions;

      release(&ptable.lock);

      return 0;
    }
  }

  release(&ptable.lock);

  return -1;
}


// ---------------------------------------------------------
// Semaphore initialization
// ---------------------------------------------------------

int
sem_init(int value)
{
  int i;

  acquire(&semlock);

  for(i = 0; i < NSEM; i++){

    if(!semtable[i].allocated){

      semtable[i].allocated = 1;
      semtable[i].value = value;

      release(&semlock);

      return i;
    }
  }

  release(&semlock);

  return -1;
}


// ---------------------------------------------------------
// Semaphore wait
// ---------------------------------------------------------

int
sem_wait(int id)
{
  if(id < 0 ||
     id >= NSEM ||
     !semtable[id].allocated)

    return -1;

  acquire(&semlock);

  while(semtable[id].value <= 0){
    sleep(&semtable[id], &semlock);
  }

  semtable[id].value--;

  release(&semlock);

  return 0;
}


// ---------------------------------------------------------
// Semaphore signal
// ---------------------------------------------------------

int
sem_signal(int id)
{
  if(id < 0 ||
     id >= NSEM ||
     !semtable[id].allocated)

    return -1;

  acquire(&semlock);

  semtable[id].value++;

  wakeup(&semtable[id]);

  release(&semlock);

  return 0;
}
