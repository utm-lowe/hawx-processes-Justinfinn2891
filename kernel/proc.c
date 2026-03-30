#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "mem.h"
#include "string.h"
#include "console.h"
#include "trap.h"
#include "proc.h"

// Extern Globals
extern pagetable_t kernel_pagetable; // mem.c
extern char trampoline[]; // trampoline.S
extern char _binary_user_init_start; // The user init code

////////////////////////////////////////////////////////////////////////////////
// Static Definitions and Helper Function Prototypes
////////////////////////////////////////////////////////////////////////////////
static int nextpid = 1;
static pagetable_t proc_pagetable(struct proc*);
static void proc_free_pagetable(pagetable_t pagetable, uint64 sz);
static void proc_freewalk(pagetable_t pagetable);
static uint64 proc_shrink(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
static int proc_loadseg(pagetable_t pagetable, uint64 va, void *bin, uint offset, uint sz);
static void proc_guard(pagetable_t pagetable, uint64 va);


////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////
struct cpu cpu;
struct proc proc[NPROC];




////////////////////////////////////////////////////////////////////////////////
// Process API Functions 
////////////////////////////////////////////////////////////////////////////////

// Initialize the proc table, and allocate a page for each process's 
// kernel stack. Map the stacks in high memory, followed by an invalid guard page.
void proc_init(void)
{
    struct proc *p;
    void *page;

      for(p = proc; p < &proc[NPROC]; p++){
    p->kstack = KSTACK(p-proc);

    page = vm_page_alloc();
    if(page == 0){
      panic("proc_init: vm_page_alloc has failed");
    }

    if(vm_page_insert(kernel_pagetable, p->kstack, (uint64)page, PTE_R | PTE_W) < 0){
      panic("proc_init: vm_page_insert has failed");
    }
    p->state = UNUSED;
    p->pid = 0;
    p->trapframe = 0;
    p->pagetable = 0;
    p->sz = 0;

    memset(&p->context, 0, sizeof(p->context));
  }

}



// Set up the first user process. Return the process it was allocated to.
struct proc*
proc_load_user_init(void)
{
    void *bin = &_binary_user_init_start;
struct proc *p = proc_alloc();
if(p == 0)
    panic("proc_load_user_init: alloc failed");

if(proc_load_elf(p, bin) < 0)
    panic("proc_load_user_init: load failed");

p->state = RUNNABLE;
    
    // Allocate a new process. If there is no process avaialble, panic.
    // Use proc_load_elf to load up the elf string. 
    // As an additional hint, I have defined the variables you need 
    // for you. The bin pointer points to the embedded BLOB which
    // contains the program image for init.
    // YOUR CODE HERE

    return p;
}


// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc* 
proc_alloc(void)
{
  struct proc* p = 0;

  for (int i = 0; i < NPROC; i++) {
      if (proc[i].state == UNUSED) {
          p = &proc[i];
          break;
      }
  }

  if(p == 0)
    return 0;

  p->pid = nextpid++;
  p->state = USED;

  p->trapframe = vm_page_alloc();
  if(p->trapframe == 0){
    p->state = UNUSED;
    return 0;
  }

  memset(p->trapframe, 0, PGSIZE);
  memset(&p->context, 0, sizeof(p->context));


  p->context.ra = (uint64)usertrapret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}


// free a proc structure and the data hanging from it,
// including user pages.
void 
proc_free(struct proc *p)
{
    
    if (p->trapframe) {
        vm_page_free(p->trapframe);
        p->trapframe = 0;
    }

    if (p->pagetable) {
        proc_free_pagetable(p->pagetable, p->sz);
        p->pagetable = 0;
    }

    p->pid = 0;
    p->sz = 0;

    printf("Yes");

    p->state = UNUSED;
}


// Load the ELF program image stored in the binary string bin
// into the specified process. This operation will destroy the 
// pagetable currently in p, and replace it with a page table
// as indicated by the segments of the elf formatted binary.
int
proc_load_elf(struct proc *p, void *bin)
{
    struct elfhdr elf;
    struct proghdr ph;
    int i, off;
    uint64 sz = 0;
    pagetable_t pagetable = 0;
    pagetable_t old_pagetable;
    elf = *(struct elfhdr*) bin;
    if(elf.magic != ELF_MAGIC)
        goto bad;

    if((pagetable = proc_pagetable(p)) == 0)
        goto bad;

    off = elf.phoff;
    for(i = 0; i < elf.phnum; i++){
        ph = *(struct proghdr*)(bin + off + (i * sizeof(struct proghdr)));
        
        if(ph.type != ELF_PROG_LOAD)
            continue;
        if(ph.memsz < ph.filesz)
            goto bad;
        if(ph.vaddr + ph.memsz < ph.vaddr)
            goto bad;

        uint64 newsz;
        if((newsz = proc_resize(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
            goto bad;
        sz = newsz;

   
        if(proc_loadseg(pagetable, ph.vaddr, bin, ph.off, ph.filesz) < 0)
            goto bad;

    }


    sz = PGROUNDUP(sz);
    uint64 newsz;
    if((newsz = proc_resize(pagetable, sz, sz + 2 * PGSIZE)) == 0)
        goto bad;
    sz = newsz;
    

    proc_guard(pagetable, sz - 2 * PGSIZE);
    
    uint64 sp = sz;
    uint64 stackbase = sp - PGSIZE;

    old_pagetable = p->pagetable;
    p->pagetable = pagetable;
    p->sz = sz;
    p->trapframe->epc = elf.entry; 
    p->trapframe->sp = sp;       

    if(old_pagetable)
        proc_free_pagetable(old_pagetable, p->sz); 

    return 0;

bad:
    if(pagetable)
        proc_free_pagetable(pagetable, sz);
    return -1;
}


// Resize the process so that it occupies newsz bytes of memory.
// If newsz > oldsz
//   Allocate PTEs and physical memory to grow process from oldsz to
// If newsz < oldsz
//   Use proc_shrink to decrease the zie of the process.
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 proc_resize(pagetable_t pagetable, uint64 oldsz, uint64 newsz) 
{    
    if(newsz > oldsz) {
    for (uint64 a = PGROUNDUP(oldsz); a < newsz; a += PGSIZE) {

        pte_t *pte = walk_pgtable(pagetable, a, 0);
        if (pte && (*pte & PTE_V)) {
            continue;
        }

        void* page = vm_page_alloc();
        if (page == 0)
            return 0;

        if (vm_page_insert(pagetable, a, (uint64)page,
            PTE_R | PTE_W | PTE_X | PTE_U) != 0) {
            vm_page_free(page);
            return 0;
        }
    }
    return newsz;
    } else {
        return proc_shrink(pagetable, oldsz, newsz);
    }
}

// Given a parent process's page table, copy its memory into a 
// child's page table. Copies both the page table and the physical
// memory.
int 
proc_vmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk_pgtable(old, i, 0)) == 0)
      panic("proc_vmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("proc_vmcopy: page not present");
    
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    if((mem = vm_page_alloc()) == 0)
      goto err;

    memmove(mem, (char*)pa, PGSIZE);

   if(vm_page_insert(new, i, (uint64)mem, flags) != 0){
        vm_page_free(mem);
        goto err;
    }
  }
  return 0;

err:
  vm_page_remove(new, 0, i / PGSIZE, 1);
  return -1;
}


////////////////////////////////////////////////////////////////////////////////
// Static Helper Functions
////////////////////////////////////////////////////////////////////////////////

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
static pagetable_t 
proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    pagetable = vm_create_pagetable();
    if (!pagetable)
        return 0;

  if(vm_page_insert(pagetable, TRAMPOLINE, (uint64)trampoline, PTE_R | PTE_X) != 0){
        vm_page_free(pagetable);
        return 0;
    }

  if(vm_page_insert(pagetable, TRAPFRAME, (uint64)p->trapframe, PTE_R | PTE_W) != 0){
        vm_page_remove(pagetable, TRAMPOLINE, 1, 0);
        vm_page_free(pagetable);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
static void 
proc_free_pagetable(pagetable_t pagetable, uint64 sz)
{
  vm_page_remove(pagetable, TRAMPOLINE, 1, 0);
  vm_page_remove(pagetable, TRAPFRAME, 1, 0);
  if (sz > 0) {
    vm_page_remove(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  }
  proc_freewalk(pagetable);
    // 1.) Remove the TRAMPOLINE and TRAPFRAME pages
    // 2.) Remove all the user memory pages, freeing their 
    //    physical memory.
    // 3.) Free the user page table.
    // Functions Used: vm_page_remove, proc_freewalk
    // YOUR CODE HERE
}



// Recursively free page-table pages.
// All leaf mappings must already have been removed.
static void 
proc_freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      proc_freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  vm_page_free((void*)pagetable);
}


// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
static uint64 
proc_shrink(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    vm_page_remove(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}


// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
proc_loadseg(pagetable_t pagetable, uint64 va, void *bin, uint offset, uint sz)
{
    uint i, n;
    uint64 pa;
    pte_t *pte;

    for(i = 0; i < sz; i += PGSIZE) {
        pte = walk_pgtable(pagetable, va + i, 0);
        if(pte == 0 || (*pte & PTE_V) == 0)
            panic("proc_loadseg: page not mapped");

        pa = PTE2PA(*pte);

        n = PGSIZE;
        if(sz - i < PGSIZE)
            n = sz - i;

        memmove((void*)pa, (char*)bin + offset + i, n);
    }

    return 0;
}


// mark a PTE invalid for user access.
// used by proc_load_elf for the user stack guard page.
static void 
proc_guard(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;

    pte = walk_pgtable(pagetable, va, 0);
    if(pte == 0)
        panic("proc_guard");
    *pte &= ~PTE_U;
}

// Find the process with the given pid and return a pointer to it.
// If the process is not found, return 0
struct proc *proc_find(int pid) {
  // Simply search the proc array, looking for the specified pid.
  // YOUR CODE HERE
  for(int i=0; i<NPROC; i++){
    if(proc[i].pid == pid){
      return &proc[i];
    }
  } 
  return 0;
}