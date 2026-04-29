#ifndef HOBBY_OS_BOARD_RASPI5_H
#define HOBBY_OS_BOARD_RASPI5_H

#define UART_BASE 0x107D001000UL

#define BOARD_NAME "raspi5"

/*
 * BCM2712 (Pi 5) interrupt controller. The Pi 5 ships a re-mapped
 * GIC-400 (v2). Distributor sits at 0x107fff9000 and the CPU
 * interface at 0x107fffa000 in BCM2712's MMIO window. PL011 UART0 is
 * SPI 121 (-> IRQ 153 once the GIC adds its 32-IRQ banked offset).
 *
 * NOTE: real-hardware bring-up will need start4.elf / config.txt to
 * map this address range and disable the legacy ARMC interrupt
 * controller. The ports below let the kernel compile against these
 * numbers; flipping BOARD_HAS_GIC on Pi 5 is a follow-up commit.
 */
#define BCM2712_GICD_BASE 0x107fff9000UL
#define BCM2712_GICC_BASE 0x107fffa000UL
#define BCM2712_IRQ_UART  153

#endif
