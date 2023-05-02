#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#ifndef PROC_H
#define PROC_H
#include "proc.h"
#endif
#include "defs.h"
#ifndef KTHREAD_H
#define KTHREAD_H
#include "kthread.h"
#endif

extern struct proc proc[NPROC];
extern void forkret(void);

// Given a proc , it allocates a unique kernel thread ID using the counter and lock inside the proc
int alloctid(struct proc *p)
{
  acquire(&p->thread_id_lock);
  int tid = p->next_thread_id;
  p->next_thread_id = p->next_thread_id+1;
  release(&p->thread_id_lock);
  return tid;
}

void kthreadinit(struct proc *p)
{
  for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
  {
    initlock(&kt->lock, "kthread");
    kt->tstate = TUNUSED;
    kt->process = p;
    // WARNING: Don't change this line!
    // get the pointer to the kernel stack of the kthread
    kt->kstack = KSTACK((int)((p - proc) * NKT + (kt - p->kthread)));
  }
}
// Fetches and returns the current running thread from the current cpu’s cpu structs.
struct kthread *mykthread()
{
  push_off();
  struct cpu *c = mycpu();
  pop_off();
  return c->thread;
}

struct trapframe *get_kthread_trapframe(struct proc *p, struct kthread *kt)
{
  return (p->base_trapframes + ((int)(kt - p->kthread)));
}

// // TODO: delte this after you are done with task 2.2
// void allocproc_help_function(struct proc *p) {
//   p->kthread->trapframe = get_kthread_trapframe(p, p->kthread);

//   p->context.sp = p->kthread->kstack + PGSIZE;
// }

// Function for allocating a new kernel thread for a process
struct kthread *allocthread(struct proc *p)
{
  struct kthread *kt;
  for (kt = p->kthread; kt < &p->kthread[NKT]; kt++)
  {
    acquire(&kt->lock);
    if (kt->tstate == TUNUSED)
    {
      kt->tstate = TUSED;
      kt->thread_id = alloctid(p);
      kt->trapframe = get_kthread_trapframe(p, kt);
      memset(&kt->context, 0, sizeof(kt->context));
      kt->context.ra = (uint64)forkret;
      kt->context.sp = kt->kstack + PGSIZE;
      return kt;
    }
    release(&kt->lock);
  }
  return 0;
}

extern void
freekthread(struct kthread *kt)
{
  /*
  Given a kthread, it sets its fields to null / zero, and the state to TUNUSED.
  */
  // if (kt->trapframe)
  //   kfree((void *)kt->trapframe);
  kt->trapframe = 0;
  kt-> kstack = 0;
  // kt->process = 0;
  kt->killed = 0;
  kt->xstate = 0;
  kt->thread_id = 0;
  kt->chan = 0;
  kt->tstate = TUNUSED;
}

