#ifndef KTHREAD_H
#define KTHREAD_H
#include "kthread.h"
#endif


enum procstate { UNUSED, USED, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;

  int next_thread_id;
  struct spinlock thread_id_lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  //void *chan;                  // If non-zero, sleeping on chan      -- DELETE IF NOT USED --
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  struct kthread kthread[NKT];        // kthread group table
  struct trapframe *base_trapframes;  // data page for trampolines

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // // these are private to the process, so p->lock need not be held.
  // uint64 kstack;               // Virtual address of kernel stack  -- DELETE IF NOT USED --
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  // struct context context;      // swtch() here to run process      -- DELETE IF NOT USED --
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
