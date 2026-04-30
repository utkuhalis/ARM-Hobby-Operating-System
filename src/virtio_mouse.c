#include <stdint.h>
#include "virtio.h"
#include "virtio_mouse.h"
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

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define REL_X     0
#define REL_Y     1
#define ABS_X     0
#define ABS_Y     1
#define BTN_LEFT  0x110
#define BTN_RIGHT 0x111

/* virtio-tablet defaults to a 0..32767 absolute coordinate range.
 * Map that into our 800x600 framebuffer. */
#define TABLET_RANGE 32767

struct virtq_desc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct virtq_avail { uint16_t flags; uint16_t idx; uint16_t ring[QSIZE]; uint16_t used_event; };
struct virtq_used_elem { uint32_t id; uint32_t len; };
struct virtq_used { uint16_t flags; uint16_t idx; struct virtq_used_elem ring[QSIZE]; uint16_t avail_event; };

struct input_event { uint16_t type; uint16_t code; uint32_t value; };

static struct virtio_mmio_dev dev;
static int initialized;

static struct virtq_desc  vqdesc[QSIZE]  __attribute__((aligned(16)));
static struct virtq_avail vqavail        __attribute__((aligned(2)));
static struct virtq_used  vqused         __attribute__((aligned(4)));
static struct input_event vqbufs[QSIZE]  __attribute__((aligned(8)));

static volatile uint16_t last_used_idx;
static volatile int32_t  cursor_x = 400;
static volatile int32_t  cursor_y = 300;
static volatile int      buttons;

int  vmouse_present(void)    { return initialized; }
int  vmouse_irq_number(void) { return initialized ? (int)dev.irq : -1; }
void vmouse_position(int32_t *x, int32_t *y) {
    if (x) *x = cursor_x;
    if (y) *y = cursor_y;
}
int  vmouse_buttons(void) { return buttons; }

void vmouse_inject_move(int dx, int dy) {
    cursor_x += dx;
    cursor_y += dy;
    if (cursor_x < 0)   cursor_x = 0;
    if (cursor_y < 0)   cursor_y = 0;
    if (cursor_x > 799) cursor_x = 799;
    if (cursor_y > 599) cursor_y = 599;
}

void vmouse_inject_button(int left_down) {
    if (left_down) buttons |=  1;
    else           buttons &= ~1;
}

static void clamp(void) {
    if (cursor_x < 0)   cursor_x = 0;
    if (cursor_y < 0)   cursor_y = 0;
    if (cursor_x > 799) cursor_x = 799;
    if (cursor_y > 599) cursor_y = 599;
}

static void handle_event(const struct input_event *ev) {
    if (ev->type == EV_REL) {
        if (ev->code == REL_X) cursor_x += (int32_t)ev->value;
        else if (ev->code == REL_Y) cursor_y += (int32_t)ev->value;
        clamp();
    } else if (ev->type == EV_ABS) {
        /* virtio-tablet: ev->value is in [0..TABLET_RANGE]; rescale */
        if (ev->code == ABS_X) {
            cursor_x = ((int64_t)ev->value * 800)  / TABLET_RANGE;
        } else if (ev->code == ABS_Y) {
            cursor_y = ((int64_t)ev->value * 600) / TABLET_RANGE;
        }
        clamp();
    } else if (ev->type == EV_KEY) {
        int bit = (ev->code == BTN_LEFT) ? 1 : (ev->code == BTN_RIGHT) ? 2 : 0;
        if (bit) {
            if (ev->value) buttons |= bit;
            else buttons &= ~bit;
        }
    }
}

static void process_used(void) {
    while (last_used_idx != vqused.idx) {
        uint16_t i = last_used_idx % QSIZE;
        uint16_t desc_idx = (uint16_t)vqused.ring[i].id;
        handle_event(&vqbufs[desc_idx]);
        uint16_t a = vqavail.idx % QSIZE;
        vqavail.ring[a] = desc_idx;
        __asm__ volatile("dmb ish" ::: "memory");
        vqavail.idx++;
        last_used_idx++;
    }
    __asm__ volatile("dmb ish" ::: "memory");
}

void vmouse_irq(void) {
    if (!initialized) return;
    uint32_t st = (uint32_t)*(volatile uint32_t *)(dev.base + MMIO_INT_STATUS);
    process_used();
    *(volatile uint32_t *)(dev.base + MMIO_INT_ACK) = st;
    *(volatile uint32_t *)(dev.base + MMIO_QUEUE_NOTIFY) = 0;
}

int vmouse_init(void) {
    /* second virtio-input on the bus is the mouse (first is keyboard) */
    if (virtio_mmio_find_nth(VIRTIO_DEVICE_INPUT, 1, &dev) != 0) {
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
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 */

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    if (!(vio_read32(&dev, MMIO_STATUS) & STATUS_FEATURES_OK)) return -2;

    vio_write32(&dev, MMIO_QUEUE_SEL, 0);
    if (vio_read32(&dev, MMIO_QUEUE_NUM_MAX) < QSIZE) return -3;
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
    vio_write32(&dev, MMIO_QUEUE_NOTIFY, 0);
    return 0;
}
