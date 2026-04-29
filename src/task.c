#include <stdint.h>
#include <stddef.h>
#include "task.h"
#include "heap.h"
#include "str.h"

#define TASK_STACK_SIZE 16384

extern void switch_context(uint64_t *old_sp, uint64_t new_sp);

static task_t  bootstrap;
static task_t *current = &bootstrap;
static task_t *all_tasks = &bootstrap; /* singly-linked list head */
static int     next_id   = 1;
static volatile int resched_pending;

static void list_append(task_t *t) {
    task_t *p = all_tasks;
    while (p->next) p = p->next;
    p->next = t;
}

static void copy_name(char *dst, const char *src) {
    size_t i = 0;
    while (i + 1 < TASK_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void task_trampoline(void) {
    task_t *t = current;
    if (t->entry) {
        t->entry(t->arg);
    }
    task_exit();
}

void task_init(void) {
    bootstrap.sp         = 0;
    bootstrap.state      = TASK_RUNNING;
    bootstrap.id         = 0;
    copy_name(bootstrap.name, "init");
    bootstrap.stack      = NULL;
    bootstrap.stack_size = 0;
    bootstrap.entry      = NULL;
    bootstrap.arg        = NULL;
    bootstrap.ran_ticks  = 0;
    bootstrap.next       = NULL;
    current   = &bootstrap;
    all_tasks = &bootstrap;
}

int task_spawn(const char *name, void (*entry)(void *), void *arg) {
    task_t *t = (task_t *)kalloc(sizeof(task_t));
    if (!t) return -1;

    t->stack = kalloc(TASK_STACK_SIZE);
    if (!t->stack) { kfree(t); return -1; }

    /*
     * Initial kernel stack frame: 12 dwords, mirroring switch_context's
     * save area. x19..x29 are zeroed; x30 is task_trampoline so the
     * first time switch_context returns, the task starts there.
     */
    uint64_t *sp = (uint64_t *)((uint8_t *)t->stack + TASK_STACK_SIZE);
    sp -= 12;
    for (int i = 0; i < 11; i++) sp[i] = 0;        /* x19..x29 */
    sp[11] = (uint64_t)(uintptr_t)task_trampoline; /* x30 -> entry */

    t->sp         = (uint64_t)(uintptr_t)sp;
    t->state      = TASK_READY;
    t->id         = next_id++;
    copy_name(t->name, name);
    t->stack_size = TASK_STACK_SIZE;
    t->entry      = entry;
    t->arg        = arg;
    t->ran_ticks  = 0;
    t->next       = NULL;

    list_append(t);
    return t->id;
}

static task_t *pick_next(void) {
    task_t *p = current->next ? current->next : all_tasks;
    task_t *start = p;
    do {
        if (p->state == TASK_READY) return p;
        p = p->next ? p->next : all_tasks;
    } while (p != start);
    return NULL;
}

void task_yield(void) {
    task_t *prev = current;
    task_t *next = pick_next();
    if (!next || next == prev) {
        return;
    }

    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    current     = next;

    switch_context(&prev->sp, next->sp);
}

void task_exit(void) {
    current->state = TASK_DEAD;
    task_yield();
    for (;;) {
        __asm__ volatile("wfi");
    }
}

task_t *task_current(void) {
    return current;
}

task_t *task_first(void) {
    return all_tasks;
}

void task_request_resched(void) {
    resched_pending = 1;
}

int task_resched_pending(void) {
    int v = resched_pending;
    resched_pending = 0;
    return v;
}
