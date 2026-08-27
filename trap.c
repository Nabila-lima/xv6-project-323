#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers

struct spinlock tickslock;
uint ticks;

// ---------------------------------------------------------
// Round Robin configuration
// ---------------------------------------------------------

#define DEFAULT_QUANTUM 10


void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);

  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3,
          vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}


void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}


//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  // -------------------------------------------------------
  // System calls
  // -------------------------------------------------------

  if(tf->trapno == T_SYSCALL){

    if(myproc()->killed)
      exit();

    myproc()->tf = tf;

    syscall();

    if(myproc()->killed)
      exit();

    return;
  }


  // -------------------------------------------------------
  // Hardware interrupts
  // -------------------------------------------------------

  switch(tf->trapno){

  case T_IRQ0 + IRQ_TIMER:

    // Global system tick.
    if(cpuid() == 0){

      acquire(&tickslock);

      ticks++;

      wakeup(&ticks);

      release(&tickslock);
    }

    lapiceoi();

    break;


  case T_IRQ0 + IRQ_IDE:

    ideintr();

    lapiceoi();

    break;


  case T_IRQ0 + IRQ_IDE+1:

    // Bochs generates spurious IDE1 interrupts.
    break;


  case T_IRQ0 + IRQ_KBD:

    kbdintr();

    lapiceoi();

    break;


  case T_IRQ0 + IRQ_COM1:

    uartintr();

    lapiceoi();

    break;


  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:

    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);

    lapiceoi();

    break;


  //PAGEBREAK: 13
  default:

    if(myproc() == 0 || (tf->cs&3) == 0){

      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d "
              "eip %x (cr2=0x%x)\n",
              tf->trapno,
              cpuid(),
              tf->eip,
              rcr2());

      panic("trap");
    }


    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid,
            myproc()->name,
            tf->trapno,
            tf->err,
            cpuid(),
            tf->eip,
            rcr2());

    myproc()->killed = 1;
  }


  // -------------------------------------------------------
  // Check whether process has been killed
  // -------------------------------------------------------

  // Force process exit if it has been killed and is in
  // user space.
  //
  // If it is still executing in the kernel, let it keep
  // running until it reaches the regular system call return.

  if(myproc() &&
     myproc()->killed &&
     (tf->cs&3) == DPL_USER)

    exit();


  // -------------------------------------------------------
  // ROUND ROBIN PREEMPTION
  // -------------------------------------------------------
  //
  // Every timer interrupt represents one CPU tick.
  //
  // For the currently running process:
  //
  //     cpu_ticks++
  //     ticks_used++
  //
  // When ticks_used reaches timeslice:
  //
  //     ticks_used = 0
  //     preemptions++
  //     yield()
  //
  // yield() changes:
  //
  //     RUNNING -> RUNNABLE
  //
  // and returns control to scheduler().
  //
  // scheduler() then selects the next RUNNABLE process
  // using circular Round Robin order.
  // -------------------------------------------------------

  if(myproc() &&
     myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0 + IRQ_TIMER){

    struct proc *p = myproc();

    // Count CPU time.
    p->stats.cpu_ticks++;

    // Count this tick as part of the current quantum.
    p->ticks_used++;

    // -----------------------------------------------------
    // Quantum expired
    // -----------------------------------------------------

    if(p->ticks_used >= p->timeslice){

      // Reset quantum counter.
      p->ticks_used = 0;

      // Count timer-based preemption.
      p->stats.preemptions++;

      // Give up CPU.
      yield();
    }
  }


  // -------------------------------------------------------
  // Final killed-process check
  // -------------------------------------------------------

  if(myproc() &&
     myproc()->killed &&
     (tf->cs&3) == DPL_USER)

    exit();
}
