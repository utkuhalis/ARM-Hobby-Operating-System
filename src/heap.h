#ifndef HOBBY_OS_HEAP_H
#define HOBBY_OS_HEAP_H

#include <stddef.h>
#include <stdint.h>

void   heap_init(void);
void  *kalloc(size_t n);
void   kfree(void *p);

size_t heap_total(void);
size_t heap_used(void);

#endif
