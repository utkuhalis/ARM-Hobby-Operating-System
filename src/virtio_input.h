#ifndef HOBBY_OS_VIRTIO_INPUT_H
#define HOBBY_OS_VIRTIO_INPUT_H

int  vinput_init(void);
int  vinput_read_char(char *out);
void vinput_irq(void);
int  vinput_irq_number(void);

uint64_t vinput_event_count(void);
uint64_t vinput_irq_count(void);
uint32_t vinput_mmio_version(void);

#endif
