#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


// ---------------------------------------------------------
// Round Robin configuration
// ---------------------------------------------------------

#define DEFAULT_QUANTUM 10


// ---------------------------------------------------------
// Process table
// ---------------------------------------------------------

struct {
  struct spinlock lock;
  struct proc proc[NPROC];

  // Round Robin:
  // Index of the process from which the next scheduler
  // search will begin.
  int rr_next;
} ptable;


static struct proc *initproc;


// ---------------------------------------------------------
// Semaphore configuration
// ---------------------------------------------------------

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


// =========================================================
// PROCESS TABLE INITIALIZATION
// =========================================================

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&semlock, "semlock");

  // Round Robin starts at the first entry.
  ptable.rr_next = 0;
}


// =========================================================
// CPU FUNCTIONS
// =========================================================

// Must be called with interrupts disabled.
int
cpuid()
{
  return mycpu() - cpus;
}


// Must be called with interrupts disabled to avoid the caller
// being rescheduled between reading lapicid and running
// through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags() & FL_IF)
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


// =========================================================
// CURRENT PROCESS
// =========================================================

// Disable interrupts so that we are not rescheduled while
// reading proc from the CPU structure.
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


// =========================================================
// PROCESS ALLOCATION
// =========================================================

// Look in the process table for an UNUSED process.
// If found, change state to EMBRYO and initialize the
// state required to run in the kernel.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->state == UNUSED)
      goto found;
  }

  release(&ptable.lock);

  return 0;


found:

  p->state = EMBRYO;
  p->pid = nextpid++;


  // -------------------------------------------------------
  // Round Robin initialization
  // -------------------------------------------------------

  p->timeslice = DEFAULT_QUANTUM;
  p->ticks_used = 0;


  // Clear all scheduling statistics.
  memset(&p->stats, 0, sizeof(p->stats));


  release(&ptable.lock);


  // -------------------------------------------------------
  // Allocate kernel stack
  // -------------------------------------------------------

  if((p->kstack = kalloc()) == 0){

    acquire(&ptable.lock);

    p->state = UNUSED;
    p->pid = 0;

    release(&ptable.lock);

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


// =========================================================
// FIRST USER PROCESS
// =========================================================

void
userinit(void)
{
  struct proc *p;

  extern char _binary_initcode_start[],
              _binary_initcode_size[];


  p = allocproc();

  initproc = p;


  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");


  inituvm(p->pgdir,
          _binary_initcode_start,
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


  safestrcpy(p->name,
             "initcode",
             sizeof(p->name));


  p->cwd = namei("/");


  // Make the process runnable.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}


// =========================================================
// GROW PROCESS MEMORY
// =========================================================

int
growproc(int n)
{
  uint sz;

  struct proc *curproc = myproc();


  sz = curproc->sz;


  if(n > 0){

    if((sz = allocuvm(curproc->pgdir,
                      sz,
                      sz + n)) == 0)

      return -1;
  }

  else if(n < 0){

    if((sz = deallocuvm(curproc->pgdir,
                        sz,
                        sz + n)) == 0)

      return -1;
  }


  curproc->sz = sz;

  switchuvm(curproc);


  return 0;
}


// =========================================================
// FORK
// =========================================================

int
fork(void)
{
  int i, pid;

  struct proc *np;
  struct proc *curproc = myproc();


  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;


  // Copy process memory.
  if((np->pgdir =
        copyuvm(curproc->pgdir,
                curproc->sz)) == 0){

    kfree(np->kstack);

    np->kstack = 0;

    acquire(&ptable.lock);

    np->state = UNUSED;
    np->pid = 0;

    release(&ptable.lock);

    return -1;
  }


  np->sz = curproc->sz;

  np->parent = curproc;

  *np->tf = *curproc->tf;


  // Clear %eax so fork returns 0 in the child.
  np->tf->eax = 0;


  for(i = 0; i < NOFILE; i++){

    if(curproc->ofile[i])
      np->ofile[i] =
        filedup(curproc->ofile[i]);
  }


  np->cwd = idup(curproc->cwd);


  safestrcpy(np->name,
             curproc->name,
             sizeof(np->name));


  pid = np->pid;


  acquire(&ptable.lock);


  // -------------------------------------------------------
  // Every new process gets a fresh Round Robin quantum.
  // -------------------------------------------------------

  np->timeslice = DEFAULT_QUANTUM;
  np->ticks_used = 0;

  memset(&np->stats, 0, sizeof(np->stats));


  // Put child into the ready queue.
  np->state = RUNNABLE;


  release(&ptable.lock);


  return pid;
}


// =========================================================
// EXIT
// =========================================================

void
exit(void)
{
  struct proc *curproc = myproc();

  struct proc *p;

  int fd;


  if(curproc == initproc)
    panic("init exiting");


  // Close open files.
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


  // Wake parent.
  wakeup1(curproc->parent);


  // Pass abandoned children to init.
  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->parent == curproc){

      p->parent = initproc;


      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }


  // Become zombie.
  curproc->state = ZOMBIE;


  // Enter scheduler.
  sched();


  panic("zombie exit");
}


// =========================================================
// WAIT
// =========================================================

int
wait(void)
{
  struct proc *p;

  int havekids, pid;

  struct proc *curproc = myproc();


  acquire(&ptable.lock);


  for(;;){

    havekids = 0;


    for(p = ptable.proc;
        p < &ptable.proc[NPROC];
        p++){

      if(p->parent != curproc)
        continue;


      havekids = 1;


      if(p->state == ZOMBIE){

        pid = p->pid;


        kfree(p->kstack);

        p->kstack = 0;


        freevm(p->pgdir);


        p->pid = 0;

        p->parent = 0;

        p->name[0] = 0;

        p->killed = 0;


        // Reset scheduling information.
        p->timeslice = DEFAULT_QUANTUM;
        p->ticks_used = 0;

        memset(&p->stats,
               0,
               sizeof(p->stats));


        p->state = UNUSED;


        release(&ptable.lock);


        return pid;
      }
    }


    if(!havekids ||
       curproc->killed){

      release(&ptable.lock);

      return -1;
    }


    // Wait for children.
    sleep(curproc, &ptable.lock);
  }
}


// =========================================================
// ROUND ROBIN SCHEDULER
// =========================================================
//
// This is the core CPU scheduler.
//
// Algorithm:
//
//      1. Start from ptable.rr_next.
//      2. Search circularly through the process table.
//      3. Find the first RUNNABLE process.
//      4. Run it.
//      5. Move rr_next to the entry after it.
//      6. When its quantum expires, trap.c calls yield().
//      7. yield() changes RUNNING -> RUNNABLE.
//      8. scheduler() selects the next process.
//
// Example:
//
//      P1 -> P2 -> P3 -> P1 -> P2 -> P3
//
// All processes normally receive:
//
//      DEFAULT_QUANTUM = 10 ticks
//
// =========================================================

void
scheduler(void)
{
  struct proc *p;

  struct cpu *c = mycpu();


  c->proc = 0;


  for(;;){

    // Enable interrupts on this CPU.
    sti();


    acquire(&ptable.lock);


    p = 0;


    // -------------------------------------------------------
    // Circular Round Robin search
    // -------------------------------------------------------

    for(int i = 0; i < NPROC; i++){

      int index =
        (ptable.rr_next + i) % NPROC;


      struct proc *candidate =
        &ptable.proc[index];


      if(candidate->state == RUNNABLE){

        p = candidate;


        // -------------------------------------------------
        // Move the RR pointer forward immediately.
        //
        // This is what prevents the same process from
        // being selected again when another runnable
        // process is waiting.
        // -------------------------------------------------

        ptable.rr_next =
          (index + 1) % NPROC;


        break;
      }
    }


    if(p != 0){

      // -----------------------------------------------------
      // Start a completely new time quantum.
      // -----------------------------------------------------

      p->ticks_used = 0;


      // Process is now executing.
      c->proc = p;

      p->state = RUNNING;


      // A scheduling decision occurred.
      p->stats.ctx_switches++;


      // Switch to process address space.
      switchuvm(p);


      // -----------------------------------------------------
      // Switch:
      //
      // scheduler -> process
      //
      // The process eventually returns here through
      // yield(), sleep(), or exit().
      // -----------------------------------------------------

      swtch(&(c->scheduler),
            p->context);


      // -----------------------------------------------------
      // We are back in the scheduler.
      // -----------------------------------------------------

      switchkvm();


      c->proc = 0;
    }


    release(&ptable.lock);
  }
}


// =========================================================
// ENTER SCHEDULER
// =========================================================

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


  if(readeflags() & FL_IF)
    panic("sched interruptible");


  intena = mycpu()->intena;


  swtch(&p->context,
        mycpu()->scheduler);


  mycpu()->intena = intena;
}


// =========================================================
// YIELD
// =========================================================
//
// Give up the CPU.
//
// The important Round Robin operation is:
//
//      RUNNING -> RUNNABLE
//
// Once the process becomes RUNNABLE, scheduler() will
// continue searching from ptable.rr_next.
//

void
yield(void)
{
  acquire(&ptable.lock);


  // Put current process back into ready queue.
  myproc()->state = RUNNABLE;


  // Enter scheduler.
  sched();


  release(&ptable.lock);
}


// =========================================================
// FORK RETURN
// =========================================================

void
forkret(void)
{
  static int first = 1;


  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);


  if(first){

    first = 0;


    // Initialization that must run in
    // process context.
    iinit(ROOTDEV);

    initlog(ROOTDEV);
  }
}


// =========================================================
// SLEEP
// =========================================================

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();


  if(p == 0)
    panic("sleep");


  if(lk == 0)
    panic("sleep without lk");


  // Must acquire ptable.lock before changing
  // process state and calling sched().
  if(lk != &ptable.lock){

    acquire(&ptable.lock);

    release(lk);
  }


  // -------------------------------------------------------
  // Put process into sleeping state.
  // -------------------------------------------------------

  p->chan = chan;

  p->state = SLEEPING;


  // Reset current quantum.
  //
  // When this process wakes up it will receive
  // a fresh Round Robin quantum.
  p->ticks_used = 0;


  sched();


  // Process has been awakened.
  p->chan = 0;


  // Reacquire original lock.
  if(lk != &ptable.lock){

    release(&ptable.lock);

    acquire(lk);
  }
}


// =========================================================
// WAKEUP
// =========================================================

static void
wakeup1(void *chan)
{
  struct proc *p;


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->state == SLEEPING &&
       p->chan == chan){

      // Sleeping process becomes runnable.
      p->state = RUNNABLE;


      // A process returning from I/O/sleep receives
      // a fresh Round Robin quantum.
      p->ticks_used = 0;
    }
  }
}


// =========================================================
// WAKEUP ALL
// =========================================================

void
wakeup(void *chan)
{
  acquire(&ptable.lock);

  wakeup1(chan);

  release(&ptable.lock);
}


// =========================================================
// KILL
// =========================================================

int
kill(int pid)
{
  struct proc *p;


  acquire(&ptable.lock);


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->pid == pid){

      p->killed = 1;


      // Wake a killed sleeping process.
      if(p->state == SLEEPING){

        p->state = RUNNABLE;

        p->ticks_used = 0;
      }


      release(&ptable.lock);


      return 0;
    }
  }


  release(&ptable.lock);


  return -1;
}


// =========================================================
// PROCESS DUMP
// =========================================================

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


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

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

      getcallerpcs(
        (uint*)p->context->ebp + 2,
        pc);


      for(i = 0;
          i < 10 && pc[i] != 0;
          i++)

        cprintf(" %p", pc[i]);
    }


    cprintf("\n");
  }
}


// =========================================================
// SET ROUND ROBIN QUANTUM
// =========================================================
//
// Optional system call.
//
// Standard Round Robin uses:
//
//      DEFAULT_QUANTUM = 10
//
// This function allows testing with another quantum.
//
// Example:
//
//      setquantum_pid(5, 20)
//
// gives PID 5 a 20-tick quantum.
//

int
setquantum_pid(int pid, int quantum)
{
  struct proc *p;


  // Prevent invalid quantum values.
  if(quantum < 1 ||
     quantum > 100)

    return -1;


  acquire(&ptable.lock);


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->pid == pid &&
       p->state != UNUSED){

      p->timeslice = quantum;


      // Restart current quantum.
      p->ticks_used = 0;


      release(&ptable.lock);


      return 0;
    }
  }


  release(&ptable.lock);


  return -1;
}


// =========================================================
// GET ROUND ROBIN QUANTUM
// =========================================================

int
gettimeslice(int pid)
{
  struct proc *p;


  acquire(&ptable.lock);


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->pid == pid &&
       p->state != UNUSED){

      int ts = p->timeslice;


      release(&ptable.lock);


      return ts;
    }
  }


  release(&ptable.lock);


  return -1;
}


// =========================================================
// GET PROCESS SCHEDULING STATISTICS
// =========================================================

int
getpstats(int pid, struct pstats *out)
{
  struct proc *p;


  if(out == 0)
    return -1;


  acquire(&ptable.lock);


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->pid == pid &&
       p->state != UNUSED){

      *out = p->stats;


      release(&ptable.lock);


      return 0;
    }
  }


  release(&ptable.lock);


  return -1;
}


// =========================================================
// GET PROCESS INFORMATION
// =========================================================

int
getprocinfo(int pid, struct procinfo *out)
{
  struct proc *p;


  if(out == 0)
    return -1;


  acquire(&ptable.lock);


  for(p = ptable.proc;
      p < &ptable.proc[NPROC];
      p++){

    if(p->pid == pid &&
       p->state != UNUSED){

      // Basic process information.
      out->pid = p->pid;

      safestrcpy(out->name,
                 p->name,
                 sizeof(out->name));

      out->state = p->state;


      // Round Robin information.
      out->timeslice = p->timeslice;


      // Scheduling statistics.
      out->cpu_ticks =
        p->stats.cpu_ticks;

      out->sleep_ticks =
        p->stats.sleep_ticks;

      out->ctx_switches =
        p->stats.ctx_switches;

      out->preemptions =
        p->stats.preemptions;


      release(&ptable.lock);


      return 0;
    }
  }


  release(&ptable.lock);


  return -1;
}


// =========================================================
// SEMAPHORE INITIALIZATION
// =========================================================

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


// =========================================================
// SEMAPHORE WAIT
// =========================================================

int
sem_wait(int id)
{
  if(id < 0 ||
     id >= NSEM ||
     !semtable[id].allocated)

    return -1;


  acquire(&semlock);


  while(semtable[id].value <= 0){

    // sleep() releases semlock while sleeping.
    sleep(&semtable[id], &semlock);
  }


  semtable[id].value--;


  release(&semlock);


  return 0;
}


// =========================================================
// SEMAPHORE SIGNAL
// =========================================================

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
