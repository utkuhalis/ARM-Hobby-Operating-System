#ifndef HOBBY_OS_VIRTIO_H
#define HOBBY_OS_VIRTIO_H

#include <stdint.h>

#define VIRTIO_DEVICE_INPUT 18

struct virtio_mmio_dev {
    volatile uint8_t *base;
    uint32_t          device_id;
    uint32_t          irq;
};

int      virtio_mmio_find(uint32_t want_device_id, struct virtio_mmio_dev *out);
int      virtio_mmio_find_nth(uint32_t want_device_id, int nth, struct virtio_mmio_dev *out);

uint32_t vio_read32(struct virtio_mmio_dev *d, uint32_t off);
void     vio_write32(struct virtio_mmio_dev *d, uint32_t off, uint32_t v);

/*
 * Flush a region of memory out of the D-cache to the point of
 * coherency. Necessary before letting a virtio device DMA-read
 * driver-written queue structures (descriptor table, avail ring,
 * indirect buffers).
 */
void virtio_dma_flush(const volatile void *p, unsigned long n);
void virtio_dma_invalidate(const volatile void *p, unsigned long n);

#endif
