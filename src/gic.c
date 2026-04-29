#include <stdint.h>
#include "gic.h"

#define GICD_BASE  0x08000000UL
#define GICC_BASE  0x08010000UL

#define GICD_CTLR        (volatile uint32_t *)(GICD_BASE + 0x000)
#define GICD_TYPER       (volatile uint32_t *)(GICD_BASE + 0x004)
#define GICD_ISENABLER(n) (volatile uint32_t *)(GICD_BASE + 0x100 + (n) * 4)
#define GICD_ICENABLER(n) (volatile uint32_t *)(GICD_BASE + 0x180 + (n) * 4)
#define GICD_ICPENDR(n)   (volatile uint32_t *)(GICD_BASE + 0x280 + (n) * 4)
#define GICD_IPRIORITYR(n) (volatile uint8_t  *)(GICD_BASE + 0x400 + (n))
#define GICD_ITARGETSR(n)  (volatile uint8_t  *)(GICD_BASE + 0x800 + (n))
#define GICD_ICFGR(n)     (volatile uint32_t *)(GICD_BASE + 0xC00 + (n) * 4)

#define GICC_CTLR  (volatile uint32_t *)(GICC_BASE + 0x000)
#define GICC_PMR   (volatile uint32_t *)(GICC_BASE + 0x004)
#define GICC_BPR   (volatile uint32_t *)(GICC_BASE + 0x008)
#define GICC_IAR   (volatile uint32_t *)(GICC_BASE + 0x00c)
#define GICC_EOIR  (volatile uint32_t *)(GICC_BASE + 0x010)

void gic_init(void) {
    *GICD_CTLR = 0;
    *GICC_CTLR = 0;

    uint32_t typer = *GICD_TYPER;
    uint32_t lines = ((typer & 0x1f) + 1) * 32;
    if (lines > 1020) lines = 1020;

    for (uint32_t i = 0; i < lines; i += 32) {
        *GICD_ICENABLER(i / 32) = 0xffffffffu;
        *GICD_ICPENDR(i / 32)   = 0xffffffffu;
    }

    for (uint32_t i = 0; i < lines; i++) {
        *GICD_IPRIORITYR(i) = 0xa0;
    }

    for (uint32_t i = 32; i < lines; i++) {
        *GICD_ITARGETSR(i) = 0x01;
    }

    *GICC_PMR  = 0xff;
    *GICC_BPR  = 0;
    *GICC_CTLR = 1;
    *GICD_CTLR = 1;
}

void gic_enable_irq(uint32_t irq) {
    *GICD_ISENABLER(irq / 32) = 1u << (irq % 32);
}

uint32_t gic_iar(void) {
    return *GICC_IAR;
}

void gic_eoi(uint32_t iar) {
    *GICC_EOIR = iar;
}
