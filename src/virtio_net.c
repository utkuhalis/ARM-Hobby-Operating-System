#include <stdint.h>
#include "virtio.h"
#include "virtio_net.h"
#include "gic.h"

#define VIRTIO_DEVICE_NET 1

#define MMIO_DEVICE_FEATURES_SEL 0x014
#define MMIO_DRIVER_FEATURES     0x020
#define MMIO_DRIVER_FEATURES_SEL 0x024
#define MMIO_QUEUE_SEL           0x030
#define MMIO_QUEUE_NUM_MAX       0x034
#define MMIO_QUEUE_NUM           0x038
#define MMIO_QUEUE_READY         0x044
#define MMIO_STATUS              0x070
#define MMIO_CONFIG              0x100

#define STATUS_ACKNOWLEDGE 0x01
#define STATUS_DRIVER      0x02
#define STATUS_DRIVER_OK   0x04
#define STATUS_FEATURES_OK 0x08

#define VIRTIO_NET_F_MAC (1u << 5)

static struct virtio_mmio_dev dev;
static int  initialized;
static uint8_t mac_addr[6];

int vnet_present(void)    { return initialized; }
int vnet_irq_number(void) { return initialized ? (int)dev.irq : -1; }
const uint8_t *vnet_mac(void) { return mac_addr; }

int vnet_init(void) {
    if (virtio_mmio_find(VIRTIO_DEVICE_NET, &dev) != 0) {
        return -1;
    }

    vio_write32(&dev, MMIO_STATUS, 0);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE);
    vio_write32(&dev, MMIO_STATUS, STATUS_ACKNOWLEDGE | STATUS_DRIVER);

    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 0);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, VIRTIO_NET_F_MAC);
    vio_write32(&dev, MMIO_DEVICE_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES_SEL, 1);
    vio_write32(&dev, MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 */

    vio_write32(&dev, MMIO_STATUS,
                STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK);
    if (!(vio_read32(&dev, MMIO_STATUS) & STATUS_FEATURES_OK)) return -2;

    /* MAC is the first 6 bytes of config space when VIRTIO_NET_F_MAC is set */
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = *(volatile uint8_t *)(dev.base + MMIO_CONFIG + i);
    }

    initialized = 1;
    return 0;
}
