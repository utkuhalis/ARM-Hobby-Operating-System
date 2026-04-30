#include <stdint.h>
#include "virtio.h"
#include "virtio_net.h"
#include "gic.h"
#include "str.h"
#include "net.h"

#define VIRTIO_DEVICE_NET 1

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

#define VIRTIO_NET_F_MAC      (1u << 5)
#define VIRTIO_NET_F_STATUS   (1u << 16)

#define DESC_F_NEXT  1
#define DESC_F_WRITE 2

#define QSIZE 32
#define FRAME_BUF_SIZE 1536  /* enough for 1500 MTU + virtio header + slack */

struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;   /* present with VIRTIO_F_VERSION_1 */
};
#define NET_HDR_SIZE 12

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

struct vqueue {
    struct virtq_desc  desc[QSIZE]   __attribute__((aligned(16)));
    struct virtq_avail avail         __attribute__((aligned(2)));
    struct virtq_used  used          __attribute__((aligned(4)));
    uint8_t            buf[QSIZE][FRAME_BUF_SIZE] __attribute__((aligned(16)));
    volatile uint16_t  last_used_idx;
};

static struct virtio_mmio_dev dev;
static int        initialized;
static uint8_t    mac_addr[6];
static volatile uint64_t rx_count, tx_count;

/* RX queue is queue 0, TX queue is queue 1 in virtio-net. */
static struct vqueue rxq __attribute__((aligned(64)));
static struct vqueue txq __attribute__((aligned(64)));

int vnet_present(void)    { return initialized; }
int vnet_irq_number(void) { return initialized ? (int)dev.irq : -1; }
const uint8_t *vnet_mac(void) { return mac_addr; }
uint64_t vnet_rx_count(void) { return rx_count; }
uint64_t vnet_tx_count(void) { return tx_count; }

static void queue_select(uint16_t q) {
    vio_write32(&dev, MMIO_QUEUE_SEL, q);
}

static void queue_install(struct vqueue *vq, int populate_for_rx) {
    if (vio_read32(&dev, MMIO_QUEUE_NUM_MAX) < QSIZE) return;
    vio_write32(&dev, MMIO_QUEUE_NUM, QSIZE);

    for (int i = 0; i < QSIZE; i++) {
        vq->desc[i].addr  = (uint64_t)(uintptr_t)vq->buf[i];
        vq->desc[i].len   = FRAME_BUF_SIZE;
        vq->desc[i].flags = populate_for_rx ? DESC_F_WRITE : 0;
        vq->desc[i].next  = 0;
        if (populate_for_rx) {
            vq->avail.ring[i] = (uint16_t)i;
        }
    }
    if (populate_for_rx) {
        vq->avail.idx = QSIZE;
    }
    vq->last_used_idx = 0;

    uint64_t da = (uint64_t)(uintptr_t)vq->desc;
    uint64_t aa = (uint64_t)(uintptr_t)&vq->avail;
    uint64_t ua = (uint64_t)(uintptr_t)&vq->used;
    vio_write32(&dev, MMIO_QUEUE_DESC_LOW,    (uint32_t)(da));
    vio_write32(&dev, MMIO_QUEUE_DESC_HIGH,   (uint32_t)(da >> 32));
    vio_write32(&dev, MMIO_QUEUE_DRIVER_LOW,  (uint32_t)(aa));
    vio_write32(&dev, MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(aa >> 32));
    vio_write32(&dev, MMIO_QUEUE_DEVICE_LOW,  (uint32_t)(ua));
    vio_write32(&dev, MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(ua >> 32));

    virtio_dma_flush(vq->desc, sizeof(vq->desc));
    virtio_dma_flush(&vq->avail, sizeof(vq->avail));

    vio_write32(&dev, MMIO_QUEUE_READY, 1);
}

static int rx_alloc_idx;

int vnet_send(const void *frame, uint32_t len) {
    if (!initialized || len > FRAME_BUF_SIZE - NET_HDR_SIZE) return -1;

    uint16_t i = txq.avail.idx % QSIZE;
    /* Build virtio header in front of frame */
    uint8_t *p = txq.buf[i];
    for (int k = 0; k < NET_HDR_SIZE; k++) p[k] = 0;
    for (uint32_t k = 0; k < len; k++) p[NET_HDR_SIZE + k] = ((const uint8_t *)frame)[k];

    txq.desc[i].addr  = (uint64_t)(uintptr_t)p;
    txq.desc[i].len   = NET_HDR_SIZE + len;
    txq.desc[i].flags = 0;
    txq.desc[i].next  = 0;

    txq.avail.ring[i] = i;
    __asm__ volatile("dmb ish" ::: "memory");
    txq.avail.idx = (uint16_t)(txq.avail.idx + 1);
    __asm__ volatile("dmb ish" ::: "memory");

    virtio_dma_flush(&txq.desc[i], sizeof(txq.desc[i]));
    virtio_dma_flush(&txq.avail,   sizeof(txq.avail));
    virtio_dma_flush(p, NET_HDR_SIZE + len);

    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 1);  /* TX queue index = 1 */
    tx_count++;
    return 0;
}

static void process_rx(void) {
    virtio_dma_invalidate(&rxq.used, sizeof(rxq.used));
    while (rxq.last_used_idx != rxq.used.idx) {
        uint16_t i = rxq.last_used_idx % QSIZE;
        struct virtq_used_elem ue = rxq.used.ring[i];
        uint16_t desc_idx = (uint16_t)ue.id;
        uint32_t total    = ue.len;

        virtio_dma_invalidate(rxq.buf[desc_idx], total);

        if (total > NET_HDR_SIZE) {
            const uint8_t *frame = rxq.buf[desc_idx] + NET_HDR_SIZE;
            net_handle_frame(frame, total - NET_HDR_SIZE);
        }
        rx_count++;

        /* re-arm the descriptor */
        uint16_t a = rxq.avail.idx % QSIZE;
        rxq.avail.ring[a] = desc_idx;
        __asm__ volatile("dmb ish" ::: "memory");
        rxq.avail.idx++;

        rxq.last_used_idx++;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    virtio_dma_flush(&rxq.avail, sizeof(rxq.avail));
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);  /* notify device of refilled rx slots */
}

void vnet_irq(void) {
    if (!initialized) return;
    uint32_t st = vio_read32(&dev, MMIO_INT_STATUS);
    process_rx();
    /* TX completions: just advance last_used_idx, frame already sent */
    virtio_dma_invalidate(&txq.used, sizeof(txq.used));
    while (txq.last_used_idx != txq.used.idx) {
        txq.last_used_idx++;
    }
    vio_write32(&dev, MMIO_INT_ACK, st);
}

int vnet_init(void) {
    if (virtio_mmio_find(VIRTIO_DEVICE_NET, &dev) != 0) {
        return -1;
    }

    vio_write32(&dev, MMIO_STATUS, 0);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 */

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    if (!(vio_read32(&dev, MMIO_STATUS) & STATUS_FEATURES_OK)) return -2;

    for (int i = 0; i < 6; i++) {
        mac_addr[i] = *(volatile uint8_t *)(dev.base + MMIO_CONFIG + i);
    }

    queue_select(0);
    queue_install(&rxq, 1);
    queue_select(1);
    queue_install(&txq, 0);

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER |
                STATUS_FEATURES_OK | STATUS_DRIVER_OK);

    initialized = 1;
    gic_enable_irq(dev.irq);

    /* Tell the device we have receive buffers ready. */
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);

    rx_alloc_idx = 0;
    return 0;
}
