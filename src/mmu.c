#include <stdint.h>
#include "mmu.h"

/*
 * Minimal AArch64 MMU bring-up: 39-bit VA, 4 KiB granule, 1 GiB block
 * descriptors at L1. Identity-maps the bottom 4 GiB:
 *
 *   [0  GiB .. 1 GiB)  -> Device-nGnRnE  (GIC, UART, virtio, ramfb)
 *   [1  GiB .. 2 GiB)  -> Normal WB      (RAM)
 *   [2  GiB .. 4 GiB)  -> Normal WB      (so the kernel can poke any
 *                                          ram address QEMU might give us)
 */

#define MAIR_DEVICE_nGnRnE 0x00ULL
#define MAIR_NORMAL_WB     0xffULL

#define ATTRIDX_DEVICE 0
#define ATTRIDX_NORMAL 1

#define DESC_BLOCK   0x1ULL
#define DESC_VALID   0x1ULL
#define DESC_AF      (1ULL << 10)
#define DESC_SH_IS   (3ULL << 8)
#define DESC_AP_RW   (0ULL << 6)  /* AP[2:1]=00 -> EL1 RW only */
#define DESC_ATTR(n) ((uint64_t)(n) << 2)

#define TCR_T0SZ_25  25
#define TCR_IRGN0_WB (1ULL << 8)
#define TCR_ORGN0_WB (1ULL << 10)
#define TCR_SH0_IS   (3ULL << 12)
#define TCR_TG0_4K   (0ULL << 14)
#define TCR_IPS_36   (1ULL << 32)

static uint64_t pgdir[512] __attribute__((aligned(4096)));

static uint64_t make_block(uint64_t pa, int attr_idx, int normal) {
    uint64_t d = (pa & ~0x3fffffffULL) | DESC_BLOCK | DESC_AF | DESC_AP_RW |
                 DESC_ATTR(attr_idx);
    if (normal) {
        d |= DESC_SH_IS;
    }
    return d;
}

void mmu_init(void) {
    uint64_t mair = (MAIR_DEVICE_nGnRnE << (8 * ATTRIDX_DEVICE)) |
                    (MAIR_NORMAL_WB     << (8 * ATTRIDX_NORMAL));
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));

    uint64_t tcr = TCR_T0SZ_25 | TCR_IRGN0_WB | TCR_ORGN0_WB |
                   TCR_SH0_IS  | TCR_TG0_4K   | TCR_IPS_36;
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));

    for (int i = 0; i < 512; i++) {
        pgdir[i] = 0;
    }
    pgdir[0] = make_block(0x00000000UL, ATTRIDX_DEVICE, 0);
    pgdir[1] = make_block(0x40000000UL, ATTRIDX_NORMAL, 1);
    pgdir[2] = make_block(0x80000000UL, ATTRIDX_NORMAL, 1);
    pgdir[3] = make_block(0xC0000000UL, ATTRIDX_NORMAL, 1);

    __asm__ volatile("dsb sy");
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)(uintptr_t)pgdir));
    __asm__ volatile("isb");

    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("ic iallu");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);   /* M  - MMU enable */
    sctlr |= (1ULL << 2);   /* C  - D-cache enable */
    sctlr |= (1ULL << 12);  /* I  - I-cache enable */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
}
