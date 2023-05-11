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
  p->next_thread_id = p->next_thread_id + 1;
  release(&p->thread_id_lock);
  return tid;
}

// This function will be called once for each process at the initialization time of xv6.
void kthreadinit(struct proc *p)
{
  initlock(&p->thread_id_lock, "nexttid");
  for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++){
    initlock(&kt->lock, "kthread");
    acquire(&kt->lock);
    kt->tstate = TUNUSED;
    kt->process = p;
    // WARNING: Don't change this line!
    // get the pointer to the kernel stack of the kthread
    kt->kstack = KSTACK((int)((p - proc) * NKT + (kt - p->kthread)));
    release(&kt->lock);
  }
}
// Fetches and returns the current running thread from the current cpu’s cpu structs.
struct kthread *mykthread()
{
  push_off();
  struct cpu *c = mycpu();
  struct kthread *kt = c->thread;
  pop_off();
  return kt;
}

struct trapframe *get_kthread_trapframe(struct proc *p, struct kthread *kt)
{
  return (p->base_trapframes + ((int)(kt - p->kthread)));
}


// Function for allocating a new kernel thread for a process
struct kthread *allocthread(struct proc *p){
  struct kthread *kt;
  for (kt = p->kthread; kt < &p->kthread[NKT]; kt++) {
    acquire(&kt->lock);
    if (kt->tstate == TUNUSED){
      kt->thread_id = alloctid(p);
      kt->tstate = TUSED;

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
  kt->killed = 0;
  kt->thread_id = 0;
  kt->trapframe = 0; //?????
  kt->xstate = 0;
  kt->chan = 0;
  kt->tstate = TUNUSED;
}


int 
kthread_create(void *(*start_func)(), void *stack, uint stack_size){
  struct proc *p = myproc();  
  struct kthread *kt;

  if((kt = allocthread(p)) == 0)
    return -1;

  kt->trapframe->sp = (uint64)stack + stack_size;
  kt->trapframe->epc = (uint64)start_func;
  kt->tstate = TRUNNABLE;
  release(&kt->lock);
  return kt->thread_id;
}

int kthread_kill(int ktid){
  struct proc *p = myproc();
  struct kthread *kt;
  for(kt = p->kthread; kt < &p->kthread[NKT]; kt++){
    acquire(&kt->lock);
    if(kt->thread_id == ktid){
      kt->killed = 1;
      if(kt->tstate == TSLEEPING)
        kt->tstate = TRUNNABLE;
      release(&kt->lock);
      return 0;
    }
    release(&kt->lock);
  }
  return -1;
}

int
kthread_killed(struct kthread *kt){
  int k;
  
  acquire(&kt->lock);
  k = kt->killed;
  release(&kt->lock);
  return k;
}

int
get_active_kthreads(struct proc *p)
{
  int count = 0;
  for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++){
    acquire(&kt->lock);
    if (kt->tstate != TUNUSED && kt->tstate != TZOMBIE)
      count++;
    release(&kt->lock);
  }
  return count;
}

void kthread_exit(int status){
  struct proc *p = myproc();
  struct kthread *kt = mykthread();

  if(get_active_kthreads(p) == 1)
    exit(status);

  acquire(&kt->lock);
  kt->xstate = status;
  kt->tstate = TZOMBIE;
  release(&kt->lock);

  wakeup(kt);
  acquire(&kt->lock);
  sched();
  panic("zombie exit");
}

int kthread_join(int ktid, int *status){
  struct proc *p = myproc();
  struct kthread *kt = mykthread();

  if (kt->thread_id == ktid)
    return -1;
    
  struct kthread *t;
  acquire(&p->lock);
  for (t = p->kthread;t < &p->kthread[NKT]; t++){
      if (t->thread_id == ktid){
        while (t->tstate != TZOMBIE){
          acquire(&t->lock);
          if(t->tstate == TUNUSED){
            release(&t->lock);
            release(&p->lock);
            return -1;
          }
          if(kt->killed){
            release(&t->lock);
            release(&p->lock);
            return -1;
          }
          release(&t->lock);
          // sleep until t state is changed + release p.lock
          sleep(t, &p->lock);
        }
        /// t->tstate = TZOMBIE
        acquire(&t->lock);
        if (copyout(p->pagetable, (uint64)status, (char *)&t->xstate, sizeof(t->xstate)) < 0){
          release(&t->lock);
          release(&p->lock);
          return -1;
        }
        freekthread(t);
        release(&t->lock);
        release(&p->lock);
        return 0;
      }
    }
    release(&p->lock);
    return -1;
}

/* This functions kills all threads except mykthread()*/
void kill_all_other_threads(){
  struct proc *p = myproc();
  struct kthread *kt = mykthread();
  struct kthread *t;
  int *status = 0;
  for(t = p->kthread; t < &p->kthread[NKT]; t++){
    if(t != kt){
      acquire(&t->lock);
      t->killed = 1;
      if(t->tstate == TSLEEPING)
        t->tstate = TRUNNABLE;
      release(&t->lock);
      kthread_join(t->thread_id, status);
    }
  }
}