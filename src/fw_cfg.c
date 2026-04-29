#include <stdint.h>
#include "fw_cfg.h"

#define FW_CFG_BASE 0x09020000UL

#define FW_CFG_SEL_OFFSET  0x08
#define FW_CFG_DMA_OFFSET  0x10

#define FW_CFG_FILE_DIR  0x0019

#define FW_CFG_DMA_CTL_ERROR   0x01
#define FW_CFG_DMA_CTL_READ    0x02
#define FW_CFG_DMA_CTL_SKIP    0x04
#define FW_CFG_DMA_CTL_SELECT  0x08
#define FW_CFG_DMA_CTL_WRITE   0x10

struct fw_cfg_file {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char     name[56];
};

struct fw_cfg_dma_access {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} __attribute__((packed));

static int strcmp_local(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void fw_cfg_dma_run(volatile struct fw_cfg_dma_access *dma) {
    uint64_t addr_be = __builtin_bswap64((uint64_t)(uintptr_t)dma);
    *(volatile uint64_t *)(FW_CFG_BASE + FW_CFG_DMA_OFFSET) = addr_be;

    uint32_t pending_be = __builtin_bswap32(FW_CFG_DMA_CTL_READ | FW_CFG_DMA_CTL_WRITE);
    while (dma->control & pending_be) {
    }
}

static int fw_cfg_dma_select_read(uint16_t key, void *buf, uint32_t len) {
    volatile struct fw_cfg_dma_access dma = {
        .control = __builtin_bswap32(((uint32_t)key << 16) |
                                     FW_CFG_DMA_CTL_SELECT |
                                     FW_CFG_DMA_CTL_READ),
        .length  = __builtin_bswap32(len),
        .address = __builtin_bswap64((uint64_t)(uintptr_t)buf),
    };
    fw_cfg_dma_run(&dma);
    return (dma.control & __builtin_bswap32(FW_CFG_DMA_CTL_ERROR)) ? -1 : 0;
}

static int fw_cfg_dma_write(uint16_t key, const void *buf, uint32_t len) {
    volatile struct fw_cfg_dma_access dma = {
        .control = __builtin_bswap32(((uint32_t)key << 16) |
                                     FW_CFG_DMA_CTL_SELECT |
                                     FW_CFG_DMA_CTL_WRITE),
        .length  = __builtin_bswap32(len),
        .address = __builtin_bswap64((uint64_t)(uintptr_t)buf),
    };
    fw_cfg_dma_run(&dma);
    return (dma.control & __builtin_bswap32(FW_CFG_DMA_CTL_ERROR)) ? -1 : 0;
}

int fw_cfg_write_named(const char *name, const void *data, uint32_t size) {
    uint32_t count_be;
    if (fw_cfg_dma_select_read(FW_CFG_FILE_DIR, &count_be, sizeof(count_be)) != 0) {
        return -1;
    }
    uint32_t count = __builtin_bswap32(count_be);

    for (uint32_t i = 0; i < count; i++) {
        struct fw_cfg_file f;
        volatile struct fw_cfg_dma_access dma = {
            .control = __builtin_bswap32(FW_CFG_DMA_CTL_READ),
            .length  = __builtin_bswap32(sizeof(f)),
            .address = __builtin_bswap64((uint64_t)(uintptr_t)&f),
        };
        fw_cfg_dma_run(&dma);
        if (dma.control & __builtin_bswap32(FW_CFG_DMA_CTL_ERROR)) {
            return -1;
        }

        if (strcmp_local(f.name, name) == 0) {
            uint16_t key = __builtin_bswap16(f.select);
            return fw_cfg_dma_write(key, data, size);
        }
    }
    return -1;
}
