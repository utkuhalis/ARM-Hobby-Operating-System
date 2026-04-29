#ifndef HOBBY_OS_EXCEPTIONS_H
#define HOBBY_OS_EXCEPTIONS_H

void exceptions_init(void);
void irq_enable(void);
void irq_disable(void);

void irq_handler(void);
void panic_unhandled(void);

#endif
