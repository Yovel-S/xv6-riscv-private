#include "uthread.h"
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

struct uthread uthreads[MAX_UTHREADS];

struct uthread *mythread;
int first_thread = 1;

struct uthread *uscheduler(){
    int max_prio = 0;
    struct uthread *max_prio_t;
    struct uthread *t;
    for(t = uthreads; t < &uthreads[MAX_UTHREADS]; t++)
        if (t->state == RUNNABLE)
            if(t->priority > max_prio){
                max_prio = t->priority;
                max_prio_t = t;
            }
    return max_prio_t;
}


void uthread_yield(){
    struct uthread *uthread = uscheduler();
    mythread->state = RUNNABLE;
    uthread->state = RUNNING;
    uswtch(&mythread->context, &uthread->context);
}

int get_empty_thread_slot(){
    int i;
    for(i=0; i<MAX_UTHREADS; i++){
        if (uthreads[i].state == FREE)
        {
            return i;
        }
    }
    return -1;
}

int uthread_create(void (*start_func)(), enum sched_priority priority){
    if(first_thread){
        uthreadinit();
        first_thread = 0;
    }
    int slot = get_empty_thread_slot();

    if(slot == -1){
        return -1;
    }
    struct uthread *new_thread = &uthreads[slot];
    memset(&new_thread->context, 0, sizeof(new_thread->context));
    new_thread->context.ra = (uint64)start_func;
    new_thread->context.sp = (uint64)&(new_thread->ustack) + STACK_SIZE - 1; //maybe without -1
    new_thread->state = RUNNABLE;
    new_thread->priority = priority;
    return 0;
}

void uthread_exit(){
    mythread->state = FREE;
    uthread_yield();
}

enum sched_priority 
uthread_set_priority(enum sched_priority priority){
    enum sched_priority prev_prio = mythread->priority;
    mythread->priority = priority;
    return prev_prio;
}

enum sched_priority 
uthread_get_priority(){
    enum sched_priority prev_prio = mythread->priority;
    return prev_prio;
}

void
uthreadinit(){
    struct uthread *t;
    for(t = uthreads; t < &uthreads[MAX_UTHREADS]; t++)
      t->state = FREE;
}

int 
uthread_start_all(){
    if(first_thread)
        return -1;
    struct uthread *uthread = uscheduler();
    uthread->state = RUNNING;
    struct uthread *t;
    uswtch(&t->context, &uthread->context);
}

struct uthread *uthread_self(){
    return mythread;
}