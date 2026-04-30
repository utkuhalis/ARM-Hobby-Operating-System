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
    int           is_user;
    void        (*user_entry)(void);
    void         *user_stack;
    uint32_t      user_stack_size;
    struct task  *next;
} task_t;

void  task_init(void);
int   task_spawn(const char *name, void (*entry)(void *), void *arg);
int   task_spawn_user(const char *name, void (*entry)(void),
                      void *user_stack, uint32_t user_stack_size);
void  task_yield(void);
void  task_exit(void);

void  task_request_resched(void);
int   task_resched_pending(void);

task_t *task_current(void);
task_t *task_first(void);

/* Find a live task by name. Returns NULL if none match. */
task_t *task_find_by_name(const char *name);

/* Mark a task DEAD so the scheduler skips it on the next yield.
 * Returns 0 on success, -1 if t is the current task or NULL. */
int     task_kill(task_t *t);

#endif
