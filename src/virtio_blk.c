#include <stdint.h>
#include <stddef.h>
#include "virtio.h"
#include "virtio_blk.h"
#include "gic.h"
#include "str.h"

#define VIRTIO_DEVICE_BLK 2

#define MMIO_DEVICE_FEATURES_SEL 0x014
#define MMIO_DRIVER_FEATURES     0x020
#define MMIO_DRIVER_FEATURES_SEL 0x024
#define MMIO_QUEUE_SEL           0x030
#define MMIO_QUEUE_NUM_MAX       0x034
#define MMIO_QUEUE_NUM           0x038
#define MMIO_QUEUE_READY         0x044
#define MMIO_QUEUE_NOTIFY        0x050
#define MMIO_INT_STATUS          0x060
#define MMIO_INT_ACK             0x064
#define MMIO_STATUS              0x070
#define MMIO_QUEUE_DESC_LOW      0x080
#define MMIO_QUEUE_DESC_HIGH     0x084
#define MMIO_QUEUE_DRIVER_LOW    0x090
#define MMIO_QUEUE_DRIVER_HIGH   0x094
#define MMIO_QUEUE_DEVICE_LOW    0x0A0
#define MMIO_QUEUE_DEVICE_HIGH   0x0A4
#define MMIO_CONFIG              0x100

#define STATUS_ACKNOWLEDGE 0x01
#define STATUS_DRIVER      0x02
#define STATUS_DRIVER_OK   0x04
#define STATUS_FEATURES_OK 0x08

#define DESC_F_NEXT  1
#define DESC_F_WRITE 2

#define QSIZE 8

#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QSIZE];
    uint16_t used_event;
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[QSIZE];
    uint16_t avail_event;
};

struct blk_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct blk_io {
    struct blk_req_hdr hdr;
    uint8_t            data[BLK_SECTOR_SIZE];
    uint8_t            status;
};

static struct virtio_mmio_dev dev;
static int  initialized;

static volatile struct virtq_desc  vqdesc[QSIZE] __attribute__((aligned(16)));
static volatile struct virtq_avail vqavail       __attribute__((aligned(2)));
static volatile struct virtq_used  vqused        __attribute__((aligned(4)));
static volatile struct blk_io      blkio         __attribute__((aligned(16)));

static volatile uint16_t last_used_idx;
static volatile int      io_pending;

static uint64_t capacity_sectors;

int vblk_present(void)        { return initialized; }
int vblk_irq_number(void)     { return initialized ? (int)dev.irq : -1; }
uint64_t vblk_capacity_sectors(void) { return capacity_sectors; }

void vblk_irq(void) {
    if (!initialized) return;
    uint32_t st = vio_read32(&dev, MMIO_INT_STATUS);
    while (last_used_idx != vqused.idx) {
        last_used_idx++;
        io_pending = 0;
    }
    vio_write32(&dev, MMIO_INT_ACK, st);
}

static int submit_and_wait(uint32_t type, uint64_t sector, void *data, int is_write) {
    blkio.hdr.type     = type;
    blkio.hdr.reserved = 0;
    blkio.hdr.sector   = sector;
    if (is_write) {
        for (int i = 0; i < BLK_SECTOR_SIZE; i++) {
            blkio.data[i] = ((const uint8_t *)data)[i];
        }
    }
    blkio.status = 0xff;

    vqdesc[0].addr  = (uint64_t)(uintptr_t)&blkio.hdr;
    vqdesc[0].len   = sizeof(blkio.hdr);
    vqdesc[0].flags = DESC_F_NEXT;
    vqdesc[0].next  = 1;

    vqdesc[1].addr  = (uint64_t)(uintptr_t)blkio.data;
    vqdesc[1].len   = BLK_SECTOR_SIZE;
    vqdesc[1].flags = DESC_F_NEXT | (is_write ? 0 : DESC_F_WRITE);
    vqdesc[1].next  = 2;

    vqdesc[2].addr  = (uint64_t)(uintptr_t)&blkio.status;
    vqdesc[2].len   = 1;
    vqdesc[2].flags = DESC_F_WRITE;
    vqdesc[2].next  = 0;

    uint16_t a = vqavail.idx % QSIZE;
    vqavail.ring[a] = 0;
    __asm__ volatile("dmb ish" ::: "memory");
    vqavail.idx = (uint16_t)(vqavail.idx + 1);
    __asm__ volatile("dmb ish" ::: "memory");

    io_pending = 1;
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);

    /* short spin; if the device never consumes the request, give up */
    uint64_t spin = 0;
    uint16_t target_used = vqavail.idx;
    while (blkio.status == 0xff && vqused.idx != target_used) {
        if (++spin > 5000000ull) return -1;
        __asm__ volatile("yield");
    }
    if (blkio.status != 0) return -2;

    if (!is_write) {
        for (int i = 0; i < BLK_SECTOR_SIZE; i++) {
            ((uint8_t *)data)[i] = blkio.data[i];
        }
    }
    return 0;
}

int vblk_read(uint64_t sector, void *buf) {
    if (!initialized) return -1;
    return submit_and_wait(VIRTIO_BLK_T_IN, sector, buf, 0);
}

int vblk_write(uint64_t sector, const void *buf) {
    if (!initialized) return -1;
    return submit_and_wait(VIRTIO_BLK_T_OUT, sector, (void *)buf, 1);
}

int vblk_init(void) {
    if (virtio_mmio_find(VIRTIO_DEVICE_BLK, &dev) != 0) {
        return -1;
    }

    vio_write32(&dev, MMIO_STATUS, 0);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 0);
    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 (bit 32) */

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    if (!(vio_read32(&dev, MMIO_STATUS) & STATUS_FEATURES_OK)) return -2;

    /* capacity (sectors) is at config + 0x00 (64-bit little-endian) */
    uint32_t lo = vio_read32(&dev, MMIO_CONFIG + 0x00);
    uint32_t hi = vio_read32(&dev, MMIO_CONFIG + 0x04);
    capacity_sectors = ((uint64_t)hi << 32) | lo;

    vio_write32(&dev, MMIO_QUEUE_SEL, 0);
    if (vio_read32(&dev, MMIO_QUEUE_NUM_MAX) < QSIZE) return -3;
    vio_write32(&dev, MMIO_QUEUE_NUM, QSIZE);

    uint64_t da = (uint64_t)(uintptr_t)vqdesc;
    uint64_t aa = (uint64_t)(uintptr_t)&vqavail;
    uint64_t ua = (uint64_t)(uintptr_t)&vqused;
    vio_write32(&dev, MMIO_QUEUE_DESC_LOW,    (uint32_t)(da));
    vio_write32(&dev, MMIO_QUEUE_DESC_HIGH,   (uint32_t)(da >> 32));
    vio_write32(&dev, MMIO_QUEUE_DRIVER_LOW,  (uint32_t)(aa));
    vio_write32(&dev, MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(aa >> 32));
    vio_write32(&dev, MMIO_QUEUE_DEVICE_LOW,  (uint32_t)(ua));
    vio_write32(&dev, MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(ua >> 32));
    vio_write32(&dev, MMIO_QUEUE_READY, 1);

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER |
                STATUS_FEATURES_OK | STATUS_DRIVER_OK);

    last_used_idx = 0;
    initialized = 1;
    gic_enable_irq(dev.irq);
    return 0;
}
