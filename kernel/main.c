#include "types.h"
#include "riscv.h"
#include "console.h"
#include "port.h"
#include "trap.h"
#include "mem.h"
#include "proc.h"
#include "scheduler.h"
#include "disk.h"
#include "string.h"
#include "tests.h"
void swtch(struct context *old, struct context *new);

// start() jumps here in supervisor mode
void
main()
{
  // initialize ports
  port_init();

  // initialize uart
  uartinit();
  printf("\n");
  printf("HAWX kernel is booting\n");
  printf("\n");
  uartflush();

  // initialize traps
  trapinit();

  //initialize virtual memory
  vm_init();

  //initialize the device interrupts
  plicinit();
  // initialize other kernel subsystems
  proc_init();
  // load up the user space
  proc_load_user_init();
  // enter the scheduler

  scheduler();

  panic("Exited Scheduler");
}
