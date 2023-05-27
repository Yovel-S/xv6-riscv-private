#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

int creationTime();
int getPgToSwap();
int phyMemToSwapFile(struct proc *p, int swap_file_idx);
int getSpaceInPhyMem(struct proc* p);
int getSpaceInSwapFile(struct proc* p);
static char global_buff[PGSIZE];


struct spinlock lock;
int next_time = 1;

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0){  // if not mapped
      #if SWAP_ALGO != NONE
        if((*pte & PTE_PG) == 0) // if not swapped out
      #endif
      panic("uvmunmap: not mapped");
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free && !(*pte & PTE_PG)){ // if not swapped out
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
    #if SWAP_ALGO != NONE
      struct proc *p = myproc();
      if(p->pid > 2) {
        for (int i = 0; i < MAX_PSYC_PAGES; i++){
          if(p->phy_mem_pgs[i].address == a) {
            p->phy_mem_pgs[i].address = UNUSED;
            p->phy_mem_pgs[i].state= UNUSED;
            p->swap_file_pgs[i].address = UNUSED;
            p->swap_file_pgs[i].state = UNUSED;
          }
        }
      }
    #endif
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    #if SWAP_ALGO != NONE
      struct proc *p = myproc();
      int swap_file_idx = -1;
      if(p->pid > 2 && p->pagetable == pagetable) {
        int phy_mem_idx = getSpaceInPhyMem(p);
        
        if(phy_mem_idx == -1){ // there is no space in physical memory
          if((swap_file_idx = getSpaceInSwapFile(p)) == -1) // there is space in swap file
            panic("uvmalloc: no more space in swap file and physical memory");
          phy_mem_idx = phyMemToSwapFile(p, swap_file_idx);
        }  
        p->phy_mem_pgs[phy_mem_idx].state = USED;
        p->phy_mem_pgs[phy_mem_idx].address = a;
        p->phy_mem_pgs[phy_mem_idx].creationTime = creationTime();
        #ifdef LAPA
          p->phy_mem_pgs[phy_mem_idx].accesscounter = 0xFFFFFFFF;
        #endif
        #ifdef NFUA
          p->phy_mem_pgs[phy_mem_idx].accesscounter = 0;
        #endif
        p->phy_mem_pgs[phy_mem_idx].offsetInSF = phy_mem_idx*PGSIZE;
      }
    #endif
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0){      // page not present
      #if SWAP_ALGO != NONE
        if(*pte & PTE_PG) {       // page in swap file
          pte_t *new_pte;
          new_pte = walk(new, i, 0);
          *new_pte &= ~PTE_V;      // page not present
          *new_pte |= PTE_PG;      // page in swap file
          *new_pte |= PTE_FLAGS(*pte); 
        }
        else
          panic("uvmcopy: page not present");
      #else
        panic("uvmcopy: page not present");
      #endif
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

// this method takes page from physical memory and insert it to the swapfile
int
phyMemToSwapFile(struct proc *p, int swap_file_idx)
{
  int phy_mem_idx;
  if((phy_mem_idx = getPgToSwap(p)) < 0)
      panic("no place to swap");
  uint64 pa = walkaddr(p->pagetable, p->phy_mem_pgs[phy_mem_idx].address);
  if(writeToSwapFile(p, (void*)pa, PGSIZE*swap_file_idx, PGSIZE) == -1)
    panic("phyMemToSwapFile: writeToSwapFile failed");
  
  p->swap_file_pgs[swap_file_idx].address = p->phy_mem_pgs[phy_mem_idx].address;
  p->swap_file_pgs[swap_file_idx].offsetInSF = swap_file_idx * PGSIZE;
  p->swap_file_pgs[swap_file_idx].state = USED;
  
  pte_t *pte = walk(p->pagetable, p->phy_mem_pgs[phy_mem_idx].address, 0);
  *pte |= PTE_PG;
  *pte &= ~PTE_V;
  
  kfree((void*)pa);
  return phy_mem_idx;
}

// takes page from swapfile and insert it to physical memory
void swapFileToPhyMem(uint64 va, pte_t *pte, char* pa, struct proc *p, int phy_mem_idx){
  int swap_file_idx = 0;
  // find page on disc
  for (swap_file_idx = 0; swap_file_idx < MAX_PSYC_PAGES;swap_file_idx++)
    if(p->swap_file_pgs[swap_file_idx].address == va) 
      break;
  
  // update disk & ram  
  p->swap_file_pgs[swap_file_idx].address = UNUSED;
  p->swap_file_pgs[swap_file_idx].state = UNUSED;
  p->phy_mem_pgs[phy_mem_idx].address = va;
  p->phy_mem_pgs[phy_mem_idx].state = USED;
  p->phy_mem_pgs[phy_mem_idx].creationTime = creationTime();
  #ifdef LAPA
    p->phy_mem_pgs[phy_mem_idx].accesscounter = 0xFFFFFFFF;
  #endif
  #ifdef NFUA
    p->phy_mem_pgs[phy_mem_idx].accesscounter = 0;
  #endif
  if(readFromSwapFile(p, global_buff, swap_file_idx*PGSIZE, PGSIZE) < 0)
    panic("readFromSwapFile failed");
}

// takes page from physical memory and insert it to swapfile
int getSpaceInSwapFile(struct proc* p){
  for (int i =0 ; i<MAX_PSYC_PAGES; i++)
    if (p->swap_file_pgs[i].state == 0)
      return i;
  panic("no more memory");
  return -1;
}

int getSpaceInPhyMem(struct proc* p){
  for (int i =0 ; i<MAX_PSYC_PAGES; i++)
    if (p->phy_mem_pgs[i].state == 0)
      return i;
  return -1;
}

void pageFault(uint64 va, pte_t *pte)
{
  struct proc *p = myproc();
  char *pa = kalloc();
  int phy_mem_idx = getSpaceInPhyMem(p);

  if(phy_mem_idx == -1) {   // no space in physical memory
    if((phy_mem_idx = getPgToSwap(p)) < 0)
      panic("no place to swap");
    uint pg_va_phy_mem = p->phy_mem_pgs[phy_mem_idx].address;
    uint64 pg_address_phy_mem = walkaddr(p->pagetable, pg_va_phy_mem);
    
    swapFileToPhyMem(va, pte, pa, p, phy_mem_idx);
    int swap_file_idx = getSpaceInSwapFile(p);

    if(writeToSwapFile(p, (void*)pg_address_phy_mem, PGSIZE*swap_file_idx, PGSIZE) == -1)
      panic("failed to write to swap file");
    kfree((void*)pg_address_phy_mem);   

    p->swap_file_pgs[swap_file_idx].state = USED;
    p->swap_file_pgs[swap_file_idx].address = pg_va_phy_mem;
    
    pte_t *swapPte = walk(p->pagetable, pg_va_phy_mem, 0);
    *swapPte = *swapPte | PTE_PG;
    *swapPte = *swapPte & ~PTE_V;     
  }
  else  // there is space in physical memory
    swapFileToPhyMem(va, pte, pa, p, phy_mem_idx);
       
  mappages(p->pagetable, va, p->sz, (uint64)pa, PTE_W | PTE_X | PTE_R | PTE_U);
  memmove((void*)pa, global_buff, PGSIZE);
  *pte = *pte & ~PTE_PG;
}

 
int NFUA_get_pg_idx(){
  int min_counter = myproc()->phy_mem_pgs[0].accesscounter;
  int min_pg_idx = 0;
  for(int i=0; i < MAX_PSYC_PAGES; i++)
    if(myproc()->phy_mem_pgs[i].accesscounter< min_counter){
      min_counter=myproc()->phy_mem_pgs[i].accesscounter;
      min_pg_idx=i;
    }
  return min_pg_idx;
}

int count_ones(uint n){ 
  int count = 0; 
  while (n){ 
      count += n & 1; //AND first bit with 1 and add result to count
      n >>= 1;        //shift right one bit and continue
  } 
  return count; 
}

int LAPA_get_pg_idx(){
  int min_counter = count_ones(myproc()->phy_mem_pgs[0].accesscounter);
  int min_pg_idx=0;
  for(int i=0; i < MAX_PSYC_PAGES; i++)
    if(count_ones(myproc()->phy_mem_pgs[0].accesscounter) < min_counter){
      min_counter =count_ones(myproc()->phy_mem_pgs[0].accesscounter);
      min_pg_idx=i;
    }
  return min_pg_idx;
}

int SCFIFO_get_pg_idx(){
  int i, pg_idx;
  int creation_time;
  pte_t * pte;
find_page:
  pg_idx = -1;
  creation_time =  myproc()->phy_mem_pgs[0].creationTime;
  for (i = 0; i < MAX_PSYC_PAGES; i++)
    if (myproc()->phy_mem_pgs[i].state && myproc()->phy_mem_pgs[i].creationTime <= creation_time){
      pg_idx = i;
      creation_time = myproc()->phy_mem_pgs[i].creationTime;
    }
  pte = walk(myproc()->pagetable, myproc()->phy_mem_pgs[pg_idx].address,0);
  if (*pte & PTE_A) { 
    *pte &= ~PTE_A; // turn off PTE_A flag
    goto find_page;
  }
  return pg_idx;
}

//return index of page to swap
int getPgToSwap(){
  #ifdef NFUA  
   return NFUA_get_pg_idx();
  #endif
  #ifdef LAPA
   return LAPA_get_pg_idx();
  #endif
  #ifdef SCFIFO
   return SCFIFO_get_pg_idx();
  #endif
  return -1;
}

void updateCounter(struct proc * p){
  pte_t * pte;
  int i;
  for (i = 0; i < MAX_PSYC_PAGES; i++) {
    if (p->phy_mem_pgs[i].state == 1){
      pte = walk(p->pagetable,p->phy_mem_pgs[i].address,0);
      if (*pte & PTE_A) { // check if page accessed 
        *pte &= ~PTE_A; // turn off PTE_A flag
         p->phy_mem_pgs[i].accesscounter = p->phy_mem_pgs[i].accesscounter >> 1;
         p->phy_mem_pgs[i].accesscounter = p->phy_mem_pgs[i].accesscounter | (1 << 31);

      }
    } 
  }
}

int creationTime() {
  if(next_time == 1)
    initlock(&lock, "creationTime");
  acquire(&lock);
  int creation_time = next_time;
  next_time += 1;
  release(&lock);
  return creation_time;
}