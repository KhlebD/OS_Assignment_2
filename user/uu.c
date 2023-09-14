#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#include "user/uthread.h"

void f1()
{
    printf("enters f1\n");
    int i = 0;
    for(;;)
    {
        printf("i = %d\n", i);
        sleep(5);
        uthread_yield();
        i++;
        if(i == 5)
        {
            uthread_set_priority(LOW);
        }     
    }
    printf("f1\n");
    uthread_exit();
}

void f2()
{
    printf("enters f2\n");
    int j = 0;
    for(;;)
    {
        printf("j = %d\n", j);
        sleep(5);
        uthread_yield();
        j++;
        if(j == 5)
        {
            uthread_set_priority(LOW);
        }
    }
    printf("f2\n");
    uthread_exit();
}

int main()
{
    //uthread_create(f1, LOW);
    uthread_create(f2, LOW);
    uthread_create(f1, HIGH);
    //uthread_create(f2, MEDIUM);
    uthread_start_all();
    exit(0);
}