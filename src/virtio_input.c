#include <stdint.h>
#include "virtio.h"
#include "virtio_input.h"
#include "virtio_mouse.h"
#include "gic.h"
#include "window.h"

#define MMIO_VERSION             0x004
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
static volatile uint64_t event_count;
static volatile uint64_t irq_count;

uint64_t vinput_event_count(void) { return event_count; }
uint64_t vinput_irq_count(void)   { return irq_count; }

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

/*
 * Linux key codes used as a keyboard fallback for cursor + click,
 * so the desktop is usable even when virtio-tablet event delivery
 * isn't reaching us (e.g. macOS Cocoa hover quirk).
 */
#define KEY_ESC   1
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_UP    103
#define KEY_DOWN  108

static void handle_event(const struct input_event *ev) {
    if (ev->type != EV_KEY) return;

    /* Cursor + click via keyboard. Process both press and release for ESC
     * so the button-release callback fires; arrows fire only on press. */
    switch (ev->code) {
    case KEY_LEFT:  if (ev->value) vmouse_inject_move(-16,  0); return;
    case KEY_RIGHT: if (ev->value) vmouse_inject_move(+16,  0); return;
    case KEY_UP:    if (ev->value) vmouse_inject_move( 0, -16); return;
    case KEY_DOWN:  if (ev->value) vmouse_inject_move( 0, +16); return;
    case KEY_ESC:   vmouse_inject_button(ev->value != 0);       return;
    default: break;
    }

    /* Shifts: 42 LSHIFT, 54 RSHIFT */
    if (ev->code == 42 || ev->code == 54) {
        shift_held = (ev->value != 0);
        return;
    }
    if (ev->value == 0) return;

    char c = shift_held ? keymap_upper[ev->code & 0xff] : 0;
    if (c == 0) c = keymap_lower[ev->code & 0xff];
    if (c != 0) {
        /* If a text-input widget has keyboard focus, route the
         * character there. Otherwise it's regular shell input. */
        if (!window_handle_keyboard(c)) {
            kbd_push(c);
        }
    }
}

static void process_used(void) {
    /* Make sure we see the device's most recent vqused.idx */
    virtio_dma_invalidate(&vqused, sizeof(vqused));
    while (last_used_idx != vqused.idx) {
        uint16_t i        = last_used_idx % QSIZE;
        uint16_t desc_idx = (uint16_t)vqused.ring[i].id;

        virtio_dma_invalidate(&vqbufs[desc_idx], sizeof(vqbufs[0]));
        handle_event(&vqbufs[desc_idx]);
        event_count++;

        /* Re-arm this descriptor on the avail ring */
        uint16_t a = vqavail.idx % QSIZE;
        vqavail.ring[a] = desc_idx;
        __asm__ volatile("dmb ish" ::: "memory");
        vqavail.idx++;

        last_used_idx++;
    }
    __asm__ volatile("dmb ish" ::: "memory");
    virtio_dma_flush(&vqavail, sizeof(vqavail));
}

void vinput_irq(void) {
    if (!initialized) return;
    irq_count++;
    uint32_t status = vio_read32(&dev, MMIO_INT_STATUS);
    process_used();
    vio_write32(&dev, MMIO_INT_ACK, status);
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);
}

static uint32_t mmio_version;
uint32_t vinput_mmio_version(void) { return mmio_version; }

int vinput_init(void) {
    /*
     * On the virt machine QEMU enumerates virtio-mmio slots from the
     * top of the bus, so the first virtio-input we find is actually
     * the tablet (which sends BTN_LEFT/RIGHT as EV_KEY events). The
     * second one is the real keyboard. Pick index 1 so this driver
     * handles keyboard scancodes only.
     */
    if (virtio_mmio_find_nth(VIRTIO_DEVICE_INPUT, 1, &dev) != 0) {
        return -1;
    }

    mmio_version = vio_read32(&dev, MMIO_VERSION);

    /* Reset and walk through the standard handshake */
    vio_write32(&dev, MMIO_STATUS, 0);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 0);
    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 */

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

    /* Flush queue setup so the device sees our writes when it
     * starts reading the rings on QueueReady=1 / DRIVER_OK. */
    virtio_dma_flush(vqdesc, sizeof(vqdesc));
    virtio_dma_flush(&vqavail, sizeof(vqavail));
    virtio_dma_flush(vqbufs, sizeof(vqbufs));

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
