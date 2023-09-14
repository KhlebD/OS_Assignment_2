#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

extern void forkret(void);

extern struct proc proc[NPROC];

void kthreadinit(struct proc *p)
{
    initlock(&p->tid_lock, "nexttid");

    for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        initlock(&kt->lock, "thread");
        kt->state = UNUSED;
        kt->my_pcb = p;
        // WARNING: Don't change this line!
        // get the pointer to the kernel stack of the kthread
        kt->kstack = KSTACK((int)((p - proc) * NKT + (kt - p->kthread)));
    }
}

struct kthread *mykthread()
{
    push_off();
    struct cpu *c = mycpu();
    struct kthread *t = c->thread;
    pop_off();
    return t;
}

int alloctid(struct proc *p)
{
    int tid;

    acquire(&p->tid_lock);
    tid = p->nexttid;
    p->nexttid = p->nexttid + 1;
    release(&p->tid_lock);

    return tid;
}

struct kthread *
alloc_kthread(struct proc *p)
{
    //printf("K alloc ENTER\n");
    struct kthread *kt;
    for (kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        acquire(&kt->lock);
        if (kt->state == UNUSED)
        {
            goto found;
        }
        else
        {
            release(&kt->lock);
        }
    }
    return 0;

found:
    kt->tid = alloctid(p);
    kt->state = USED;
    kt->trapframe = get_kthread_trapframe(p, kt);
    kt->my_pcb = p;

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&kt->context, 0, sizeof(kt->context));
    kt->context.ra = (uint64)forkret;
    kt->context.sp = kt->kstack + PGSIZE;

    // printf("K alloc EXIT\n");
    return kt;
}

void free_kthread(struct kthread *kt)
{
    kt->tid = 0;
    kt->chan = 0;
    kt->killed = 0;
    kt->xstate = 0;
    kt->my_pcb = 0;
    kt->trapframe = 0;// to add
    kt->state = UNUSED;
    memset(&kt->context, 0 ,sizeof(kt->context));
    //memset(&kt, 0 ,sizeof(kt));
    release(&kt->lock);
    // if (kt->trapframe)
    //     kfree((void *)kt->trapframe);
    // kt->trapframe = 0;
}

struct trapframe *get_kthread_trapframe(struct proc *p, struct kthread *kt)
{
    return p->base_trapframes + ((int)(kt - p->kthread));
}

int kthread_killed(struct kthread *kt)
{
    int k;
    acquire(&kt->lock);
    k = kt->killed;
    release(&kt->lock);
    return k;
}