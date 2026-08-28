// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;            // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

// ---------------------------------------------------------
// CPU Scheduling Statistics
// ---------------------------------------------------------

struct pstats {
  uint cpu_ticks;       // Total CPU ticks used
  uint sleep_ticks;     // Total ticks spent sleeping
  uint runnable_ticks;  // Total ticks spent runnable
  uint ctx_switches;    // Number of context switches
  uint preemptions;     // Number of timer-based preemptions
};

// Information returned about a process
struct procinfo {
  int pid;
  char name[16];
  int state;

  // Round Robin information
  int timeslice;

  // Scheduling statistics
  uint cpu_ticks;
  uint sleep_ticks;
  uint ctx_switches;
  uint preemptions;
};


enum procstate {
  UNUSED,
  EMBRYO,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
};


// ---------------------------------------------------------
// Per-process state
// ---------------------------------------------------------

struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name

  // -------------------------------------------------------
  // Round Robin Scheduling
  // -------------------------------------------------------

  int timeslice;               // Fixed CPU time quantum
  int ticks_used;               // Ticks used in current quantum

  // -------------------------------------------------------
  // Scheduling Statistics
  // -------------------------------------------------------

  struct pstats stats;
};


// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap


// Get scheduling statistics for a process.
int getpstats(int pid, struct pstats *out);

// Walk the process table once per global tick and update
// sleep_ticks / runnable_ticks for every process currently
// SLEEPING or RUNNABLE. Called from trap.c.
void update_sleep_stats(void);
