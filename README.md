# Hobby ARM Operating System

A minimal, hand-rolled AArch64 kernel built from scratch for fun and learning.

It boots, it talks to a UART, it says hello. That's it — for now.

The same source tree builds for two targets:

| Target      | Where it runs                                    |
|-------------|--------------------------------------------------|
| `qemu-virt` | Any host with QEMU — macOS, Linux, Windows-on-ARM, Windows x64 |
| `raspi5`    | Raspberry Pi 5 real hardware (SD card boot)      |

---

## What it does

`make run` opens a QEMU window and the kernel paints "Hello, World" onto a
PL011-style framebuffer (RAM-backed, served to QEMU via `ramfb`):

![Hello, World on QEMU](assets/qemu-hello-world.png)

`make run-serial` skips the window and dumps the same boot to a serial
console, which is useful on hosts without a display:

```
$ make run-serial
Hello, World
Hobby ARM OS booted
framebuffer ready
```

Then the CPU parks in `wfi` forever. Close the QEMU window, or in the
serial flow press `Ctrl-A` then `X` to quit.

## How it works

1. CPU starts at `_start` in [`src/boot.S`](src/boot.S).
2. Non-zero cores park in `wfi`.
3. Core 0 sets up a stack, zeroes `.bss`, and branches to `kernel_main` in C.
4. `kernel_main` writes `"Hello, World"` to a PL011 UART.
5. On QEMU `virt`, it then talks to the QEMU `fw_cfg` device, hands it a
   ramfb config (XRGB8888, 800×600), clears the framebuffer, and draws
   `"Hello, World"` glyph-by-glyph using a tiny built-in 8×8 bitmap font.
6. Halt loop.

The PL011 register layout is identical between QEMU's `virt` machine and the Raspberry Pi 5, so the only per-board differences are:

- the UART base address (`src/board/<name>.h`)
- the kernel load address (`linker/<name>.ld`)
- whether the board has a `ramfb`-style framebuffer (`BOARD_HAS_RAMFB`)

## Requirements

- `aarch64-elf-gcc` and `aarch64-elf-binutils` — cross compiler
- `qemu-system-aarch64` — for running without hardware
- `make`

### macOS

```
brew install qemu aarch64-elf-gcc aarch64-elf-binutils
```

### Linux

```
sudo apt install qemu-system-arm gcc-aarch64-linux-gnu
make CROSS=aarch64-linux-gnu-
```

### Windows on ARM

Install QEMU from [qemu.org](https://www.qemu.org/download/), grab a prebuilt
`aarch64-elf` GCC toolchain (or use WSL), then run `make` the same way.

## Build & run

```
# Default target is qemu-virt
make                # build kernel.elf + kernel.img
make run            # boot in a QEMU window (graphical, ramfb)
make run-serial     # boot to a serial console in the terminal
make screenshot     # boot headless and dump the framebuffer to PNG

# Raspberry Pi 5 image
make BOARD=raspi5
# → build/raspi5/kernel_2712.img
```

### Booting on a real Raspberry Pi 5

1. Format an SD card as FAT32.
2. Copy the Pi firmware files (`bootcode.bin`, `start4.elf`, `fixup4.dat`,
   `bcm2712-rpi-5-b.dtb`, etc.) from the official
   [Raspberry Pi firmware repo](https://github.com/raspberrypi/firmware) onto it.
3. Copy `build/raspi5/kernel_2712.img` to the SD card root.
4. Add a `config.txt` with:

   ```
   arm_64bit=1
   kernel=kernel_2712.img
   enable_uart=1
   ```

5. Wire up a USB-to-UART adapter to GPIO14/15, open it at 115200 8N1, boot the Pi.
6. You should see `Hello, world`.

## Project layout

```
hobby-os/
├── Makefile
├── README.md
├── assets/
│   └── qemu-hello-world.png
├── src/
│   ├── boot.S           # AArch64 entry, stack + .bss setup
│   ├── kernel.c         # kernel_main
│   ├── uart.c, uart.h   # PL011 driver
│   ├── fb.c, fb.h       # ramfb framebuffer + glyph drawing
│   ├── fw_cfg.c, fw_cfg.h  # QEMU fw_cfg DMA client
│   ├── font.c, font.h   # tiny 8x8 bitmap glyphs
│   └── board/
│       ├── qemu-virt.h  # UART_BASE + BOARD_HAS_RAMFB
│       └── raspi5.h     # UART_BASE for Pi 5
├── linker/
│   ├── qemu-virt.ld     # load at 0x40000000
│   └── raspi5.ld        # load at 0x80000
└── scripts/
    ├── run-qemu.sh
    └── screenshot.sh
```

## Roadmap

This is a hobby project, but the rough order of upcoming work is:

- [ ] GIC + timer interrupts
- [ ] MMU + paging
- [ ] Memory allocator
- [ ] Cooperative scheduler
- [ ] Pre-emptive scheduler with multiple cores
- [ ] A simple filesystem
- [ ] User-mode processes

## License

MIT.
