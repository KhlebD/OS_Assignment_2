#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "uthread.h"

void update_priorities();

struct uthread threads[MAX_UTHREADS];
struct uthread *curr_thread;

static int num_threads = 0;
static int started = 0;

int uthread_create(void (*start_func)(), enum sched_priority priority)
{
    if (num_threads >= MAX_UTHREADS)
    {
        return -1; // table is full
    }

    // find a free entry in the table
    int i;
    for (i = 0; i < MAX_UTHREADS; i++)
    {
        if (threads[i].state == FREE)
        {
            //curr_thread = &threads[i];
            break;
        }
    }
    if (i == MAX_UTHREADS)
    {
        return -1; // table is full
    }

    threads[i].priority = priority;
    threads[i].state = RUNNABLE;
    threads[i].context.ra = (uint64)start_func;
    // set thread's context registers
    threads[i].context.sp = (uint64)&threads[i].ustack + STACK_SIZE;
    
    num_threads++;
    return 0;
}

void uthread_yield()
{
    struct uthread *next_thread = 0;
    next_thread = scheduler();

    if (next_thread != 0)
    {
        curr_thread->state = RUNNABLE;
        struct context *curr_context = &curr_thread->context;
        struct context *next_context = &next_thread->context;
        curr_thread = next_thread;
        curr_thread->state = RUNNING;
        uswtch(curr_context, next_context);
    }
}

void uthread_exit()
{
    num_threads--;
    curr_thread->state = FREE;
    if (num_threads == 0)
    {
        exit(0);
    }
    struct uthread *next_thread = 0;
    next_thread = scheduler();

    if (next_thread != 0)
    {
        struct context *curr_context = &curr_thread->context;
        struct context *next_context = &next_thread->context;
        curr_thread = next_thread;
        uswtch(curr_context, next_context);
    }
}
enum sched_priority uthread_set_priority(enum sched_priority priority)
{
    enum sched_priority ret = curr_thread->priority;
    curr_thread->priority = priority;
    return ret;
}

enum sched_priority uthread_get_priority()
{
    return curr_thread->priority;
}

int uthread_start_all()
{

    // check if this is the first call to uthread_start_all
    if (num_threads == 0 || started == 1)
    {
        printf("first call fails\n");
        return -1; // return error
    }
    started = 1;

    struct uthread *first_thread = 0;
    first_thread = scheduler();
    if (first_thread != 0)
    {
        struct context trash;
        curr_thread = first_thread;
        curr_thread->state = RUNNING;
        uswtch(&trash, &curr_thread->context);
    }
    printf("no thread\n");
    return -1;
}

struct uthread *uthread_self()
{
    return curr_thread;
}

struct uthread *scheduler()
{
    struct uthread *index;
    
    struct uthread *ret = curr_thread;
    int max_priority = LOW;
    if(curr_thread == 0) 
        index = &threads[0];
    
    else if (curr_thread == &threads[MAX_UTHREADS - 1])
        index = &threads[0];

    else
        index = curr_thread + 1;
    for (int i = 0; i < MAX_UTHREADS; i++)
    {
        if ((index->priority > max_priority && index->state == RUNNABLE) || (max_priority == LOW && index->priority == LOW && index->state == RUNNABLE))
        {
            ret = index;
            max_priority = index->priority;
        }

        if (index == &threads[MAX_UTHREADS - 1])
            index = &threads[0];
        else
            index++;
    }
    return ret;
}