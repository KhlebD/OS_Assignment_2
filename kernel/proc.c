#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
int kthead_killed(struct kthread *p);

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++)
    {
        for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
        {
            char *pa = kalloc();
            if (pa == 0)
                panic("kalloc");
            uint64 va = KSTACK((int)((p - proc) * NKT + (kt - p->kthread)));
            kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
        }
    }
}

// initialize the proc table.
void procinit(void)
{
    struct proc *p;

    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    for (p = proc; p < &proc[NPROC]; p++)
    {
        initlock(&p->lock, "proc");
        p->state = UNUSED;
        kthreadinit(p);
    }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->thread->my_pcb;
    pop_off();
    return p;
}

int allocpid()
{
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->state == UNUSED)
        {
            goto found;
        }
        else
        {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = allocpid();
    p->state = USED;
    p->nexttid = 1;

    // Allocate a trapframe page.
    if ((p->base_trapframes = (struct trapframe *)kalloc()) == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // TODO: delte this after you are done with task 2.2
    // allocproc_help_function(p);
    alloc_kthread(p);
    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
    if (p->base_trapframes)
        kfree((void *)p->base_trapframes);
    p->base_trapframes = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
    for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        acquire(&kt->lock);
        free_kthread(kt);
    }
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                 (uint64)trampoline, PTE_R | PTE_X) < 0)
    {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe page just below the trampoline page, for
    // trampoline.S.
    if (mappages(pagetable, TRAPFRAME(0), PGSIZE,
                 (uint64)(p->base_trapframes), PTE_R | PTE_W) < 0)
    {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME(0), 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
    struct proc *p;

    p = allocproc();
    initproc = p;

    // allocate one user page and copy initcode's instructions
    // and data into it.
    uvmfirst(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // prepare for the very first "return" from kernel to user.
    p->kthread[0].trapframe->epc = 0;     // user program counter
    p->kthread[0].trapframe->sp = PGSIZE; // user stack pointer
    p->kthread[0].state = RUNNABLE;
    release(&p->kthread[0].lock);
    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    // p->state = RUNNABLE;

    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint64 sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0)
    {
        if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
        {
            return -1;
        }
    }
    else if (n < 0)
    {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
    // printf("fork1\n");
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();
    struct kthread *kt = mykthread();

    // Allocate process.
    if ((np = allocproc()) == 0)
    {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
    {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->state = USED;
    np->sz = p->sz;

    // copy saved user registers.
    *(np->kthread[0].trapframe) = *(kt->trapframe);

    // Cause fork to return 0 in the child.
    np->kthread[0].trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;
    release(&np->kthread[0].lock);
    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    acquire(&np->kthread[0].lock);
    np->kthread[0].state = RUNNABLE;
    release(&np->kthread[0].lock);
    release(&np->lock);
    // printf("fork 2\n");
    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
    // printf("reparent\n");
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
        if (pp->parent == p)
        {
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

void exit(int status)
{
    // printf("full exit\n");
    // printf("EXIT %d %d %d\n", cpuid(), myproc()->pid, mykthread()->tid);
    struct proc *p = myproc();
    acquire(&p->lock);

    release(&p->lock);
    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++)
    {
        if (p->ofile[fd])
        {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);
    // parent might be sleeping
    wakeup(p->parent);
    acquire(&p->lock);
    p->xstate = status;
    p->state = ZOMBIE;
    release(&p->lock);

    for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        if (kt != mykthread() && kt->state != UNUSED)
        {
            acquire(&kt->lock);
            kt->xstate = status;
            kt->state = ZOMBIE;
            release(&kt->lock);
            kthread_join(kt->tid, 0);
        }
    }

    acquire(&mykthread()->lock);
    mykthread()->xstate = status;
    mykthread()->state = ZOMBIE;

    release(&wait_lock);
    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

void kthread_exit(int status)
{
    //printf("kthread exit! %d %d %d\n", cpuid(), myproc()->pid, mykthread()->tid);
    struct proc *p = myproc();

    int counter = 0;
    for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        acquire(&kt->lock);
        if (kt->state == RUNNABLE || kt->state == RUNNING || kt->state == SLEEPING || kt->state == USED)
        {

            counter++;
        }
        release(&kt->lock);
    }
    if (counter == 1)
    {
        acquire(&mykthread()->lock);
        mykthread()->xstate = status;
        mykthread()->state = ZOMBIE;
        release(&mykthread()->lock);
        exit(status);
    }
    else
    {
        
        //printf("kthread ELSE! %d %d %d\n", cpuid(), myproc()->pid, mykthread()->tid);
        acquire(&mykthread()->lock);
        
        mykthread()->xstate = status;
        mykthread()->state = ZOMBIE;
        release(&mykthread()->lock);
        wakeup(mykthread());
        acquire(&mykthread()->lock);
        // acquire(&mykthread()->lock);
        // Jump into the scheduler, never to return.
        sched();
        panic("zombie exit");
    }
}

int kthread_create(void *(*start_func)(), void *stack, uint stack_size)
{
    struct kthread *kt = alloc_kthread(myproc());
    kt->trapframe->epc = (uint64)start_func;
    // kt->kstack = (uint64)stack; // in group they said to remove it, but if I do there is kernel trap
    kt->trapframe->sp = (uint64)stack + stack_size;
    kt->state = RUNNABLE;
    release(&kt->lock);
    // maybe realse the kt key??? (was aquired in alloc_kthread)
    return kt->tid;
}

int kthread_id()
{
    return mykthread()->tid;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr) // TODO
{
    // printf("enters wait CPU %d\n", cpuid());
    struct proc *pp;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;)
    {
        // Scan through table looking for exited children.
        havekids = 0;
        for (pp = proc; pp < &proc[NPROC]; pp++)
        {
            if (pp->parent == p)
            {
                // make sure the child isn't still in exit() or swtch().
                acquire(&pp->lock);

                havekids = 1;
                if (pp->state == ZOMBIE)
                {
                    // Found one.
                    pid = pp->pid;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                             sizeof(pp->xstate)) < 0)
                    {
                        release(&pp->lock);
                        release(&wait_lock);
                        printf("FAIL COPYOUT\n");
                        return -1;
                    }
                    freeproc(pp);
                    release(&pp->lock);
                    release(&wait_lock);
                    // printf("wait NORMAL exit CPU: %d\n", cpuid());
                    return pid;
                }
                release(&pp->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || killed(p))
        {
            // printf("wait fail no kids\n");
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

int kthread_join(int ktid, int *status)
{
    // printf("JOIN CPU%d PROC%d THREAD%d\n", cpuid(), myproc()->pid, mykthread()->tid);
    //printf("JOIN on TID %d \n", ktid);
    struct proc *p = myproc();
    struct kthread *kt;

    for (kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        acquire(&kt->lock);
        if (kt->tid == ktid)
        {
            release(&kt->lock);
            break;
        }
        release(&kt->lock);
    }
    if(kt == 0)
        return -1;
    acquire(&p->lock);

    for (;;)
    {
        if (kt->state == ZOMBIE)
        {
            acquire(&kt->lock);
            if (status != 0 && copyout(p->pagetable, (uint64)status, (char *)&kt->xstate,
                                       sizeof(kt->xstate)) < 0)
            {
                release(&kt->lock);
                release(&p->lock);
                return -1;
            }

            free_kthread(kt);
            release(&p->lock);
            return 0;
        }
        if (kt == 0  || kthread_killed(kt))
        {
            release(&p->lock);
            return -1;
        }
        
        sleep(kt, &p->lock);
    }
}
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
    // printf("scheduler\n");
    struct proc *p;
    struct cpu *c = mycpu();

    c->thread = 0;
    for (;;)
    {
        // Avoid deadlock by ensuring that devices can interrupt.
        intr_on();
        for (p = proc; p < &proc[NPROC]; p++)
        {
            // acquire(&p->lock);
            if (p->state == USED)
            {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
                {
                    acquire(&kt->lock);
                    // if(kt->state == ZOMBIE)
                    //     printf("ZOMBIE?\n");

                    if (kt->state == RUNNABLE && p->state == USED)
                    {
                        kt->state = RUNNING;
                        c->thread = kt;
                        swtch(&c->context, &kt->context);
                        c->thread = 0;
                        // c->proc = 0;
                    }
                    release(&kt->lock);
                }
                // Process is done running for now.
                // It should have changed its p->state before coming back.
            }
            // release(&p->lock);
        } // for p
    }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
    int intena;
    // struct proc *p = myproc();
    struct kthread *kt = mykthread();
    if (!holding(&kt->lock))
        panic("sched kt->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (kt->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&kt->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void) /// mayan : think about the locks fron scheduler before cntnt switch
{
    // printf("Yield\n");
    //  struct proc *p = myproc();
    struct kthread *kt = mykthread();
    acquire(&kt->lock); // TODO whre to realse
    kt->state = RUNNABLE;
    sched();
    release(&kt->lock);
    // printf("sched thread\n");
    // release(&myproc()->lock);
    // printf("sched proc\n");
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
    static int first = 1;

    // Still holding p->lock from scheduler.

    release(&mykthread()->lock); // Still holding kt->lock from scheduler. TODO
    // release(&myproc()->lock);

    if (first)
    {

        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        first = 0;
        fsinit(ROOTDEV);
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk) // TODO
{
    // printf("SLEEP %d %d %d\n", cpuid(), myproc()->pid, mykthread()->tid);
    struct kthread *kt = mykthread();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&kt->lock); // DOC: sleeplock1
    release(lk);

    // Go to sleep.
    kt->chan = chan;
    kt->state = SLEEPING;
    // printf("GOES TO SCHED! CPU%d PROC%d THREAD%d\n", cpuid(), myproc()->pid, mykthread()->tid);
    sched();
    // printf("GOES BACK FROM SCHED! CPU%d PROC%d THREAD%d\n", cpuid(), myproc()->pid, mykthread()->tid);
    // Tidy up.
    kt->chan = 0;
    // printf("return sched sleep CPU %d\n",cpuid());
    //  Reacquire original lock.

    release(&kt->lock);
    // printf("sleep thread\n");
    // release(&myproc()->lock);
    // printf("sched proc\n");
    acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
// dasgsdgsdg
void wakeup(void *chan) // TODO
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {

        acquire(&p->lock);
        for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
        {
            if (kt != mykthread())
            {
                acquire(&kt->lock);
                if (kt->state == SLEEPING && kt->chan == chan)
                {
                    kt->state = RUNNABLE;
                }
                release(&kt->lock);
            }
        }
        release(&p->lock);
    }
}
// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->pid == pid)
        {
            p->killed = 1;
            for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
            {
                acquire(&kt->lock);
                kt->killed = 1;
                if (kt->state == SLEEPING)
                {
                    kt->state = RUNNABLE;
                }
                release(&kt->lock);
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

int kthread_kill(int ktid)
{
    int ret = -1;
    struct proc *p = myproc();
    acquire(&p->lock);
    for (struct kthread *kt = p->kthread; kt < &p->kthread[NKT]; kt++)
    {
        acquire(&kt->lock);
        if (kt->tid == ktid)
        {
            kt->killed = 1;
            if (kt->state == SLEEPING)
            {
                kt->state = RUNNABLE;
            }
            ret = 0;
        }
        release(&kt->lock);
    }
    release(&p->lock);
    return ret;
}

void setkilled(struct proc *p)
{
    acquire(&p->lock);
    p->killed = 1;
    release(&p->lock);
}

int killed(struct proc *p)
{
    int k;

    acquire(&p->lock);
    k = p->killed;
    release(&p->lock);
    return k;
}

int kthead_killed(struct kthread *kt)
{
    int k;

    acquire(&kt->lock);
    k = kt->killed;
    release(&kt->lock);
    return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    struct proc *p = myproc();
    if (user_dst)
    {
        return copyout(p->pagetable, dst, src, len);
    }
    else
    {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    struct proc *p = myproc();
    if (user_src)
    {
        return copyin(p->pagetable, dst, src, len);
    }
    else
    {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
        [UNUSED] "unused",
        [USED] "used",
        [ZOMBIE] "zombie"};
    struct proc *p;
    char *state;

    printf("\n");
    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}
