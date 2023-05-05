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
struct spinlock wait_lock;

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
  struct kthread *kt = c->thread;
  pop_off();
  return kt;
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
  // printf("allocthread\n");
  struct kthread *kt;
  for (kt = p->kthread; kt < &p->kthread[NKT]; kt++)
  {
    acquire(&kt->lock);
    if (kt->tstate == TUNUSED)
    {
      kt->thread_id = alloctid(p);
      kt->tstate = TUSED;
      kt->trapframe = get_kthread_trapframe(p, kt);
      memset(&kt->context, 0, sizeof(kt->context));
      kt->context.ra = (uint64)forkret;
      kt->context.sp = kt->kstack + PGSIZE;
      // printf("allocthread: thread_id = %d\n", kt->thread_id);
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
  kt->xstate = 0;
  kt->chan = 0;
  kt->tstate = TUNUSED;
}


int 
kthread_create(void *(*start_func)(), void *stack, uint stack_size){
  if(stack_size != KTHREAD_STACK_SIZE)
    return -1;

  struct proc *p = myproc();
  acquire(&p->lock);
  
  struct kthread *kt;
  if((kt = allocthread(p)) == 0){
    release(&p->lock);
    return -1;
  }
  release(&p->lock);

  kt->trapframe->epc = (uint64)start_func;
  kt->context.sp = (uint64)stack + stack_size;
  kt->context.ra = (uint64)start_func;
  kt->tstate = TRUNNABLE;
  release(&kt->lock);
  return kt->thread_id;
}

/*This function sets the killed flag of the kthread with the given ktid in the
same process. If the kernel thread is sleeping, it also sets its state to
runnable. Upon success this function returns 0, and -1 if it fails (e.g.,
when the ktid does not match any kthread ’s ktid under this process).
Note: in order for this system call to have any effect, the killed flag of the
kthread needs to be checked in certain places and kthread_exit(…) should be
called accordingly.
Hint: look where proc’s killed flag is checked in proc.c, trap.c and sysproc.c*/
int kthread_kill(int ktid){
  struct proc *p = myproc();
  struct kthread *kt;
  acquire(&p->lock);
  for(kt = p->kthread; kt < &p->kthread[NKT]; kt++){
    acquire(&kt->lock);
    if(kt->thread_id == ktid){
      kt->killed = 1;
      if(kt->tstate == TSLEEPING){
        kt->tstate = TRUNNABLE;
      } 
      release(&kt->lock);
      release(&p->lock);
      return 0;
    }
    release(&kt->lock);
  }
  release(&p->lock);
  return -1;
}

int
kthread_killed(struct kthread *kt)
{
  int k;
  
  acquire(&kt->lock);
  k = kt->killed;
  release(&kt->lock);
  return k;
}

void kthread_exit(int status){
  /*
  This function terminates the execution of the calling thread. If called by a
  thread (even the main thread) while other threads exist within the same
  process, it shouldn’t terminate the whole process. The number given in
  status should be saved in the thread’s structure as its exit status
  */
  struct proc *p = myproc();
  struct kthread *kt = mykthread();

  acquire(&p->lock);
  kt->xstate = status;
  kt->tstate = ZOMBIE;
  p->state = RUNNABLE;
  release(&p->lock);
  wakeup(kt);

  acquire(&p->lock);
  sched();
  panic("zombie exit");
}

int kthread_join(int ktid, int *status){
  /*
  This function suspends the execution of the calling thread until the target
  thread (indicated by the argument ktid) terminates. When successful, the
  pointer given in status is filled with the exit status (if it’s not null), and the
  function returns zero. Otherwise, -1 should be returned to indicate an
  error.
  Note: calling this function with a ktid of an already terminated (but not yet
  joined) kthread should succeed and allow fetching the exit status of the
  kthread.
  */
  struct proc *p = myproc();
  struct kthread *kt = mythread();
  // thread cannot wait for himself
  if (kt->thread_id == ktid)
    return -1;
  struct kthread *curr_t;
  for (curr_t = p->kthread;curr_t < &p->kthread[NKT]; curr_t++)
  {
      acquire(&wait_lock);
      // found
      if (curr_t->thread_id == ktid)
      {
        while (curr_t->tstate != ZOMBIE)
        {
          // sleep until curr_t state is changed + release p.lock
          sleep(curr_t, &wait_lock);
          //if (kt->should_exit || kt->killed)
          if(kt->killed)
          {
            release(&wait_lock);
            return -1;
          }
        }
        if (curr_t->tstate == ZOMBIE)
        {
          acquire(&p->lock);
          // copyout the status of the exited thread
          if (status != 0 && copyout(p->pagetable, status, (char *)&curr_t->xstate, sizeof(curr_t->xstate)) < 0)
          {
            release(&p->lock);
            release(&wait_lock);
            return -1;
          }
          freethread(curr_t);
          release(&p->lock);
          release(&wait_lock);
          return 0;
        }
      }
      release(&wait_lock);
    }
}

/* This functions kills all threads except mykthread()*/
void kill_all_other_threads(){
  struct proc *p = myproc();
  struct kthread *kt = mykthread();
  struct kthread *t;
  int *status;
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