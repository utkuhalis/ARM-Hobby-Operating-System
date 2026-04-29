#include <stdint.h>
#include <stddef.h>
#include "heap.h"

#define HEAP_SIZE (2 * 1024 * 1024)
#define ALIGN     16

typedef struct header {
    struct header *next;
    size_t         size;  /* total block size including header */
} header_t;

static uint8_t  heap_region[HEAP_SIZE] __attribute__((aligned(ALIGN)));
static header_t *free_list;
static size_t   bytes_used;

static size_t round_up(size_t n) {
    return (n + (ALIGN - 1)) & ~(size_t)(ALIGN - 1);
}

void heap_init(void) {
    free_list = (header_t *)(void *)heap_region;
    free_list->next = NULL;
    free_list->size = HEAP_SIZE;
    bytes_used = 0;
}

void *kalloc(size_t n) {
    if (n == 0) return NULL;

    size_t total = round_up(n + sizeof(header_t));
    header_t **prev = &free_list;
    header_t *blk;

    while ((blk = *prev) != NULL) {
        if (blk->size >= total) {
            if (blk->size >= total + sizeof(header_t) + ALIGN) {
                header_t *rest = (header_t *)((uint8_t *)blk + total);
                rest->next = blk->next;
                rest->size = blk->size - total;
                *prev = rest;
                blk->size = total;
            } else {
                *prev = blk->next;
            }
            blk->next = NULL;
            bytes_used += blk->size;
            return (void *)((uint8_t *)blk + sizeof(header_t));
        }
        prev = &blk->next;
    }
    return NULL;
}

void kfree(void *p) {
    if (!p) return;
    header_t *blk = (header_t *)((uint8_t *)p - sizeof(header_t));
    bytes_used -= blk->size;
    blk->next = free_list;
    free_list = blk;
}

size_t heap_total(void) {
    return HEAP_SIZE;
}

size_t heap_used(void) {
    return bytes_used;
}
