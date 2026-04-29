#include <stdint.h>
#include "virtio.h"
#include "virtio_input.h"
#include "gic.h"

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

#define STATUS_ACKNOWLEDGE 0x01
#define STATUS_DRIVER      0x02
#define STATUS_DRIVER_OK   0x04
#define STATUS_FEATURES_OK 0x08

#define DESC_F_WRITE 2

#define QSIZE 64

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

struct input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

#define EV_KEY 0x01

static struct virtio_mmio_dev dev;
static struct virtq_desc      vqdesc[QSIZE]   __attribute__((aligned(16)));
static struct virtq_avail     vqavail         __attribute__((aligned(2)));
static struct virtq_used      vqused          __attribute__((aligned(4)));
static struct input_event     vqbufs[QSIZE]   __attribute__((aligned(8)));

static volatile uint16_t last_used_idx;
static int               initialized;

#define KBD_BUF 64
static volatile uint8_t  kbd_buf[KBD_BUF];
static volatile uint32_t kbd_head, kbd_tail;
static int               shift_held;

static void kbd_push(char c) {
    uint32_t next = (kbd_head + 1) % KBD_BUF;
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = (uint8_t)c;
    kbd_head = next;
}

int vinput_read_char(char *out) {
    if (kbd_tail == kbd_head) return 0;
    *out = (char)kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF;
    return 1;
}

int vinput_irq_number(void) {
    return initialized ? (int)dev.irq : -1;
}

/*
 * Linux event-codes -> ASCII for the rows we actually care about.
 * Anything else maps to 0 and is silently dropped.
 */
static const char keymap_lower[256] = {
    [1]  = 27,    [2]  = '1', [3]  = '2', [4]  = '3', [5]  = '4',
    [6]  = '5',   [7]  = '6', [8]  = '7', [9]  = '8', [10] = '9',
    [11] = '0',   [12] = '-', [13] = '=',
    [14] = '\b',  [15] = '\t',
    [16] = 'q',   [17] = 'w', [18] = 'e', [19] = 'r', [20] = 't',
    [21] = 'y',   [22] = 'u', [23] = 'i', [24] = 'o', [25] = 'p',
    [26] = '[',   [27] = ']', [28] = '\n',
    [30] = 'a',   [31] = 's', [32] = 'd', [33] = 'f', [34] = 'g',
    [35] = 'h',   [36] = 'j', [37] = 'k', [38] = 'l',
    [39] = ';',   [40] = '\'', [41] = '`',
    [43] = '\\',
    [44] = 'z',   [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b',
    [49] = 'n',   [50] = 'm',
    [51] = ',',   [52] = '.', [53] = '/',
    [57] = ' ',
};

static const char keymap_upper[256] = {
    [2]  = '!',   [3]  = '@', [4]  = '#', [5]  = '$', [6]  = '%',
    [7]  = '^',   [8]  = '&', [9]  = '*', [10] = '(', [11] = ')',
    [12] = '_',   [13] = '+',
    [16] = 'Q',   [17] = 'W', [18] = 'E', [19] = 'R', [20] = 'T',
    [21] = 'Y',   [22] = 'U', [23] = 'I', [24] = 'O', [25] = 'P',
    [26] = '{',   [27] = '}',
    [30] = 'A',   [31] = 'S', [32] = 'D', [33] = 'F', [34] = 'G',
    [35] = 'H',   [36] = 'J', [37] = 'K', [38] = 'L',
    [39] = ':',   [40] = '"', [41] = '~',
    [43] = '|',
    [44] = 'Z',   [45] = 'X', [46] = 'C', [47] = 'V', [48] = 'B',
    [49] = 'N',   [50] = 'M',
    [51] = '<',   [52] = '>', [53] = '?',
    [57] = ' ',
};

static void handle_event(const struct input_event *ev) {
    if (ev->type != EV_KEY) return;

    /* Shifts: 42 LSHIFT, 54 RSHIFT */
    if (ev->code == 42 || ev->code == 54) {
        shift_held = (ev->value != 0);
        return;
    }
    if (ev->value == 0) return;

    char c = shift_held ? keymap_upper[ev->code & 0xff] : 0;
    if (c == 0) c = keymap_lower[ev->code & 0xff];
    if (c != 0) kbd_push(c);
}

static void process_used(void) {
    while (last_used_idx != vqused.idx) {
        uint16_t i        = last_used_idx % QSIZE;
        uint16_t desc_idx = (uint16_t)vqused.ring[i].id;

        handle_event(&vqbufs[desc_idx]);

        /* Re-arm this descriptor on the avail ring */
        uint16_t a = vqavail.idx % QSIZE;
        vqavail.ring[a] = desc_idx;
        __asm__ volatile("dmb ish" ::: "memory");
        vqavail.idx++;

        last_used_idx++;
    }
    __asm__ volatile("dmb ish" ::: "memory");
}

void vinput_irq(void) {
    if (!initialized) return;
    uint32_t status = vio_read32(&dev, MMIO_INT_STATUS);
    process_used();
    vio_write32(&dev, MMIO_INT_ACK, status);
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);
}

int vinput_init(void) {
    if (virtio_mmio_find(VIRTIO_DEVICE_INPUT, &dev) != 0) {
        return -1;
    }

    /* Reset and walk through the standard handshake */
    vio_write32(&dev, MMIO_STATUS, 0);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 0);
    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 0);

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    if (!(vio_read32(&dev, MMIO_STATUS) & STATUS_FEATURES_OK)) {
        return -2;
    }

    /* Queue 0 is the event queue for virtio-input */
    vio_write32(&dev, MMIO_QUEUE_SEL, 0);
    if (vio_read32(&dev, MMIO_QUEUE_NUM_MAX) < QSIZE) {
        return -3;
    }
    vio_write32(&dev, MMIO_QUEUE_NUM, QSIZE);

    for (int i = 0; i < QSIZE; i++) {
        vqdesc[i].addr  = (uint64_t)(uintptr_t)&vqbufs[i];
        vqdesc[i].len   = sizeof(struct input_event);
        vqdesc[i].flags = DESC_F_WRITE;
        vqdesc[i].next  = 0;
        vqavail.ring[i] = (uint16_t)i;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    vqavail.idx = QSIZE;

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
    initialized   = 1;

    gic_enable_irq(dev.irq);

    /* Tell the device we have buffers ready */
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);

    return 0;
}
