#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "uthread.h"

void kthread_start_func(void)
{
    for (int i = 0; i < 10; i++)
    {
        sleep(1); // simulate work
    }
    kthread_exit(0);
    printf("kthread_exit failed\n");
    exit(1);
}
int main(void)
{
    uint64 stack_a = (uint64)malloc(STACK_SIZE);
    uint64 stack_b = (uint64)malloc(STACK_SIZE);

    printf("first create\n");
    int kt_a = kthread_create((void *(*)())kthread_start_func, (void *)stack_a, STACK_SIZE);
    printf("first passed\n");
    if (kt_a <= 0)
    {
        printf("first create fail\n");
        exit(1);
    }
    printf("second create\n");
    int kt_b = kthread_create((void *(*)())kthread_start_func, (void *)stack_b, STACK_SIZE);
    if (kt_a <= 0)
    {
        printf("second create fail\n");
        exit(1);
    }
    printf("first join\n");
    int joined = kthread_join(kt_a, 0);
    if (joined != 0)
    {
        printf("join fail 1\n");
        exit(1);
    }
    printf("%p\n", kt_a);
    printf("%p\n", kt_b);
    printf("second join\n");
    joined = kthread_join(kt_b, 0);
    if (joined != 0)
    {
        printf("join fail 1\n");
        exit(1);
    }
    free((void *)stack_a);
    free((void *)stack_b);
    return 0;
}

