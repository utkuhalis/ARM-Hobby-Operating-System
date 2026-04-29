BOARD   ?= qemu-virt

CROSS   ?= aarch64-elf-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy

SRC     := src
BUILD   := build/$(BOARD)

CFLAGS  := -ffreestanding -nostdlib -nostartfiles \
           -mcpu=cortex-a72 -mgeneral-regs-only \
           -fno-pic -fno-stack-protector \
           -O2 -Wall -Wextra -Wpedantic -std=c11 \
           -I$(SRC) -include $(SRC)/board/$(BOARD).h
ASFLAGS := -ffreestanding -nostdlib -nostartfiles -mcpu=cortex-a72
LDFLAGS := -nostdlib -nostartfiles -Wl,-T,linker/$(BOARD).ld -Wl,--build-id=none

CORE_C  := $(SRC)/kernel.c $(SRC)/uart.c $(SRC)/str.c $(SRC)/console.c \
           $(SRC)/shell.c $(SRC)/fs.c $(SRC)/sysinfo.c $(SRC)/psci.c

ifeq ($(BOARD),qemu-virt)
C_SRCS  := $(CORE_C) $(SRC)/fb.c $(SRC)/fb_console.c $(SRC)/fw_cfg.c $(SRC)/font.c
else
C_SRCS  := $(CORE_C)
endif
S_SRCS  := $(wildcard $(SRC)/*.S)
OBJS    := $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(C_SRCS)) \
           $(patsubst $(SRC)/%.S,$(BUILD)/%.o,$(S_SRCS))

ELF     := $(BUILD)/kernel.elf
IMG     := $(BUILD)/kernel.img

ifeq ($(BOARD),raspi5)
PI_IMG  := $(BUILD)/kernel_2712.img
ALL     := $(ELF) $(IMG) $(PI_IMG)
else
ALL     := $(ELF) $(IMG)
endif

.PHONY: all run clean

all: $(ALL)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.S | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(IMG): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(PI_IMG): $(IMG)
	cp $< $@

QEMU_BASE := -M virt -cpu cortex-a72 -m 256M -device ramfb

run: $(ELF)
	qemu-system-aarch64 $(QEMU_BASE) -display none -serial stdio -kernel $<

run-graphic: $(ELF)
	qemu-system-aarch64 $(QEMU_BASE) -display cocoa -serial stdio -kernel $<

screenshot: $(ELF)
	bash scripts/screenshot.sh

clean:
	rm -rf build
