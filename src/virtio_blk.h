#ifndef HOBBY_OS_VIRTIO_BLK_H
#define HOBBY_OS_VIRTIO_BLK_H

#include <stdint.h>

#define BLK_SECTOR_SIZE 512

int  vblk_init(void);
int  vblk_present(void);
int  vblk_irq_number(void);
uint64_t vblk_capacity_sectors(void);

int  vblk_read(uint64_t sector, void *buf512);
int  vblk_write(uint64_t sector, const void *buf512);

void vblk_irq(void);

#endif
