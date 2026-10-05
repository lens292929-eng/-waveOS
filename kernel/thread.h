#ifndef THREAD_H
#define THREAD_H

#include "types.h"

#define THREAD_MAX       16
#define THREAD_NAME_MAX  16

#define THREAD_RUNNING  0
#define THREAD_READY    1
#define THREAD_BLOCKED  2
#define THREAD_DEAD     3

struct thread {
    u64  rsp;
    u64  stack_base;
    u64  stack_size;
    int  state;
    int  id;
    char name[THREAD_NAME_MAX];
};

void thread_init(void);
void thread_create(const char *name, void (*fn)(void));
void thread_yield(void);
struct thread *thread_current(void);

#endif