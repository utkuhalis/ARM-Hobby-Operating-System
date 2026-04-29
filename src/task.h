#ifndef HOBBY_OS_TASK_H
#define HOBBY_OS_TASK_H

#include <stdint.h>

#define TASK_NAME_MAX 24

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD,
} task_state_t;

typedef struct task {
    uint64_t      sp;
    task_state_t  state;
    int           id;
    char          name[TASK_NAME_MAX];
    void         *stack;
    uint32_t      stack_size;
    void        (*entry)(void *);
    void         *arg;
    uint64_t      ran_ticks;
    struct task  *next;
} task_t;

void  task_init(void);
int   task_spawn(const char *name, void (*entry)(void *), void *arg);
void  task_yield(void);
void  task_exit(void);

task_t *task_current(void);
task_t *task_first(void);

#endif
