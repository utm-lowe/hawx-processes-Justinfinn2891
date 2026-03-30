#include "userlib/userlib.h"

// User Space Daemons
void start_daemons();
void idle_daemon();


int main()
{
  int i;
  int pid;

  printf("Elf Loading...PASSED\n");
  // Bring up the rest of userspace (which is not much, just yet!)
  start_daemons();
  printf("Daemons Started...PASSED\n");
    // Test out cloning and scheduling
  printf("Scheduler and Clone test...");
  if(!clone()) {
    for(i=0; i<10; i++) {
      printf("a");
    }
    terminate(getpid());
  } else {
    if(!clone()) {
      for(i=0; i<10; i++) {
        printf("b");
      }
      terminate(getpid());
    }
  }

  // See if we can kill our children
  pid = clone();
  if(!pid) {
    for(;;) {
      printf("amok");
      for(i=0; i<100000000; i++); // <-- busy wait to slow it down
    }
  }
  for(i=0; i<1000000000; i++); // <-- busy wait to slow it down
  terminate(pid);

  printf("%s\n", status(pid)==-1 ? "....PASSED" : "...FAILED");
  return 0;
}

// Start up the user space daemons
void start_daemons() 
{
  // Start the idle daemon
  if(!clone()) { idle_daemon(); }
}

// Just spin. This makes interrupt handling a little easier to get
// right.
void idle_daemon()
{
  while(1);
}
