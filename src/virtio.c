#include <stdint.h>
#include "virtio.h"

#define VIRTIO_MMIO_BASE     0x0a000000UL
#define VIRTIO_MMIO_STRIDE   0x200
#define VIRTIO_MMIO_SLOTS    32
#define VIRTIO_MMIO_IRQ_BASE 48 /* SPI 16 = IRQ 48 on QEMU virt */

#define MMIO_MAGIC      0x000
#define MMIO_VERSION    0x004
#define MMIO_DEVICE_ID  0x008

#define MAGIC_VALUE 0x74726976u /* "virt" little-endian */

uint32_t vio_read32(struct virtio_mmio_dev *d, uint32_t off) {
    return *(volatile uint32_t *)(d->base + off);
}

void vio_write32(struct virtio_mmio_dev *d, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(d->base + off) = v;
}

int virtio_mmio_find(uint32_t want_id, struct virtio_mmio_dev *out) {
    return virtio_mmio_find_nth(want_id, 0, out);
}

#define CACHE_LINE 64

void virtio_dma_flush(const volatile void *p, unsigned long n) {
    uintptr_t a   = (uintptr_t)p & ~(uintptr_t)(CACHE_LINE - 1);
    uintptr_t end = (uintptr_t)p + n;
    for (; a < end; a += CACHE_LINE) {
        __asm__ volatile("dc cvac, %0" :: "r"(a) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

void virtio_dma_invalidate(const volatile void *p, unsigned long n) {
    uintptr_t a   = (uintptr_t)p & ~(uintptr_t)(CACHE_LINE - 1);
    uintptr_t end = (uintptr_t)p + n;
    __asm__ volatile("dsb sy" ::: "memory");
    for (; a < end; a += CACHE_LINE) {
        __asm__ volatile("dc ivac, %0" :: "r"(a) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

int virtio_mmio_find_nth(uint32_t want_id, int nth, struct virtio_mmio_dev *out) {
    int seen = 0;
    for (int i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        volatile uint8_t *base =
            (volatile uint8_t *)(VIRTIO_MMIO_BASE + (uint64_t)i * VIRTIO_MMIO_STRIDE);
        uint32_t magic = *(volatile uint32_t *)(base + MMIO_MAGIC);
        if (magic != MAGIC_VALUE) continue;
        uint32_t did = *(volatile uint32_t *)(base + MMIO_DEVICE_ID);
        if (did != want_id) continue;
        if (seen++ < nth) continue;
        out->base       = base;
        out->device_id  = did;
        out->irq        = VIRTIO_MMIO_IRQ_BASE + (uint32_t)i;
        return 0;
    }
    return -1;
}
