[![Open in Codespaces](https://classroom.github.com/assets/launch-codespace-2972f46106e565e64193e422d61a12cf1da4916b45550586e14ef0a7c637dd04.svg)](https://classroom.github.com/open-in-codespaces?assignment_repo_id=23326129)
Introduction
============
In this assignment, you will be implementing the proc subsystem which will
enable the kernel to load a user init program. This init program will
eventually be responsible for bringing up the user space component of
the kernel. For now, it will just start an idle daemon and then try
out some of the system calls. If you like, you can play with the code
in `user/init.c` to see what it's like programming for our system.

Remember that we do not have any notion of a filesystem in the HAWX
kernel, and so things will be a little bit different than they are in
xv6. We will be loading our init elf object from a binary string 
embedded as a BLOB in the kernel image. This image is the compiled
version of `user/init.c`. Note that `init.c` uses the functions in our
little `userlib` library. User programs in hawx begin execution in the
userlib's `user_start` function.

Procedure
=========
Go ahead and run
    make qemu
As expected, this does not work at all! You will need to add code to complete
the following functions in `proc.c`:
  - `proc_init`
  - `proc_load_user_init`
  - `proc_alloc`
  - `proc_free`
  - `proc_load_elf`
  - `proc_resize`
  - `proc_pagetable` 
  - `proc_free_pagetable`
  - `proc_loadseg`
  - `proc_vmcopy`

As always, these functions have analogous functions in xv6. The xv6
analogs can be found in:
  - `xv6-riscv/kernel/proc.c`
  - `xv6-riscv/kernel/exec.c`
  - `xv6-riscv/kernel/vm.c`
You will also want to pay attention to the kernel's header files.
These are your reference to the rest of the kernel!

This assignment will require you to understand the HAWX memory
functions in order to adapt the xv6 code. There are plenty of hints in
the comments for you to work with. For the most part, these functions
are all interrelated, so you will need to implement all of them in
order for this to work at all! In some cases, the deviation is mild,
but in others it will be quite significant. `proc_load_elf` is
possibly the hardest one. Once you get this to work properly, you will
have completed a major right of passage as a programmer! You'll also
have demonstrated that you understand the difference between our
micro-kernel and MIT's monolithic kernel. 

Now, before we can get anything to work, you need to implement the
scheduler in `scheduler.c`. You can do this by directly adapting the
xv6 scheduler, or you can get creative. The choice is yours! If you
play with other scheduling algorithms, I will give you extra credit.
Especially if you modify `init.c` in a way that demonstrates the
effectiveness of your scheduler. 

Once you get everything done, you should be able to run the system and
see something like this:

    $ make qemu
    HAWX kernel is booting
    
    Elf Loading...PASSED
    Daemons Started...PASSED
    Scheduler and Clone test...aaaaaaaaaabbbbbbbbbbamokamokamokamokamokamokamokamoka
    mokamokamok....PASSED

Note that your number of "amoks" may vary as will the ordering of your
a's and b's. That's determined by your processor speed and how your
process get scheduled. You should see all of this on the screen
though. If you think about it, you've had an operating system
simultaneously running 4 processes! That's pretty cool!

### Hints / How to Succeed

This assignment is arguably your most significant "rite of passage" as a systems
programmer. You are moving from managing static hardware to managing the "life"
of a program. Here is how to navigate the transition from the monolithic world
of xv6 to our intrepid little HAWX microkernel:

* **The "BLOB" mercy:** In xv6, `exec` has to navigate the complexities of the
   file system and `readi` calls. In HAWX, we don’t have a file system yet, so we
   are loading from a **binary string (BLOB)** embedded directly in the kernel
   image. When you implement `proc_loadseg`, your "disk" is just an array of
   bytes in memory. Your secret weapon here is `memmove` using the expression
   `bin + offset + i`.
* **Study the Analogs:** I have pointed you toward `kernel/proc.c`,
   `kernel/exec.c`, and `kernel/vm.c` in the xv6 source. Use them as a
   reference, but **do not copy-paste**. HAWX adheres strictly to the C calling
   convention, which simplifies things, but you must manually wire the
   HAWX-specific memory functions you wrote in previous labs, like
   `vm_page_alloc` and `vm_page_insert`.
   **Baby-proof the Kernel:** ELF loading is inherently risky because the header
   contains addresses provided by the user. To succeed, you must think like a
   paranoid resource manager. Check if `ph.vaddr + ph.memsz` overflows. If a
   user tries to trick the kernel into overwriting itself, you must catch it
   and "kill the offending process" with extreme prejudice.
* **Mind the Permissions:** Remember the "hexadecimal incantations" from the
    Virtual Memory lab. When setting up the page table, ensure program text is
    readable and executable (`PTE_R | PTE_X`), but **not writable**. Use
    `proc_guard` to mark the stack guard page invalid (`~PTE_U`) so that a stack
    overflow results in a clean page fault rather than a silent kernel
    corruption.
* **The Simplified Stack:** We are not putting program arguments
    (`argc`/`argv`) on the stack for this lab. This is a mercy! It allows you to
    focus entirely on the ELF parsing and the **trapframe** setup. Just make
    sure the stack pointer (`sp`) is correctly aligned and that the `epc` is set
    to the entry point defined in the ELF header.
* **Lauer’s Law:** As you work through `proc.c`, remember that **less code is
    better code**. If your implementation of `proc_load_elf` starts looking like
    a "dim shadow of UNIX" with hundreds of lines of boilerplate, step back and
    see if you can use the helper functions more effectively.

Once you get that first user process to cycle through its states and output its
diagnostic "amoks," take a moment to celebrate. You have stitched the segments
together and delivered the spark. It is alive—now try to keep your creation from
rampaging through the kernel.