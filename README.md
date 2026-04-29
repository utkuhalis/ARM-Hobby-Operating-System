# Hobby ARM Operating System

A minimal, hand-rolled AArch64 kernel built from scratch for fun and learning.

It boots, draws "Hello, World" onto a framebuffer, and drops you into a tiny
interactive shell with a RAM-backed filesystem and a handful of system commands.

The same source tree builds for two targets:

| Target      | Where it runs                                    |
|-------------|--------------------------------------------------|
| `qemu-virt` | Any host with QEMU — macOS, Linux, Windows-on-ARM, Windows x64 |
| `raspi5`    | Raspberry Pi 5 real hardware (SD card boot)      |

---

## What it does

`make run` boots the kernel and gives you a shell on the serial console:

```
Hobby ARM OS v0.2
booted on qemu-virt, ARM Cortex-A72, EL1
type 'help' for commands

hobby# uname -a
Hobby ARM OS v0.2 aarch64 qemu-virt ARM Cortex-A72
hobby# write notes Hobby ARM OS calisiyor
wrote 23 bytes to notes
hobby# ls
  notes                            23 bytes
hobby# cat notes
Hobby ARM OS calisiyor
hobby# halt
system halted.
```

`make run-graphic` opens an actual QEMU window in addition to the shell —
the kernel paints "Hello, World" onto a ramfb-backed framebuffer:

![Hello, World on QEMU](assets/qemu-hello-world.png)

## Built-in commands

| Command           | Description                              |
|-------------------|------------------------------------------|
| `help`            | List all commands                        |
| `echo <text...>`  | Print arguments                          |
| `clear`           | Clear the screen (ANSI)                  |
| `ls`              | List files in the RAM filesystem         |
| `cat <name>`      | Print a file                             |
| `write <name> <text...>` | Create or overwrite a file        |
| `touch <name>`    | Create an empty file                     |
| `rm <name>`       | Delete a file                            |
| `uname [-a]`      | OS / arch / board / CPU info             |
| `cpuinfo`         | MIDR, MPIDR, current EL, counter freq    |
| `meminfo`         | Kernel image + stack layout, fs usage    |
| `uptime`          | Seconds since boot (from `cntpct_el0`)   |
| `halt`            | Power off via PSCI `SYSTEM_OFF`          |
| `reboot`          | Reset via PSCI `SYSTEM_RESET`            |

The filesystem holds up to 16 files of 4 KB each, all in RAM — there is no
backing store yet, so contents vanish at reboot.

## How it works

1. CPU starts at `_start` in [`src/boot.S`](src/boot.S).
2. Non-zero cores park in `wfi`.
3. Core 0 sets up a stack, zeroes `.bss`, and branches to `kernel_main` in C.
4. `kernel_main` initializes the UART and the in-memory filesystem.
5. On QEMU `virt`, it then talks to the QEMU `fw_cfg` device, hands it a
   ramfb config (XRGB8888, 800×600), clears the framebuffer, and draws
   "Hello, World" glyph-by-glyph using a small built-in 8×8 bitmap font.
6. Prints a boot banner and drops into the shell loop.
7. The shell reads a line over UART, tokenizes on whitespace, dispatches
   to a command function from a static table.

The PL011 register layout is identical between QEMU's `virt` machine and the
Raspberry Pi 5, so the per-board differences boil down to:

- the UART base address (`src/board/<name>.h`)
- the kernel load address (`linker/<name>.ld`)
- whether the board has a `ramfb` framebuffer (`BOARD_HAS_RAMFB`)
- a board name used by `uname` and `meminfo`

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
make run            # boot to a serial shell in your terminal
make run-graphic    # also open a QEMU window with the framebuffer
make screenshot     # boot headless and dump the framebuffer to PNG

# Raspberry Pi 5 image
make BOARD=raspi5
# → build/raspi5/kernel_2712.img
```

Press `Ctrl-A` then `X` to quit the serial shell. In `run-graphic`, close the
QEMU window or use the `halt` command from the shell.

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

5. Wire up a USB-to-UART adapter to GPIO14/15, open it at 115200 8N1, boot
   the Pi. The shell prompt comes up over the serial line; commands like
   `cpuinfo` will report the actual Pi 5 CPU (Cortex-A76).

## Project layout

```
hobby-os/
├── Makefile
├── README.md
├── assets/
│   └── qemu-hello-world.png
├── src/
│   ├── boot.S            # AArch64 entry, stack + .bss setup
│   ├── kernel.c          # kernel_main, boot banner
│   ├── uart.c, uart.h    # PL011 driver (TX + RX)
│   ├── console.c, console.h  # putc/puts/printf, line editor
│   ├── str.c, str.h      # mem*/str*/printf core
│   ├── shell.c, shell.h  # parser + command table
│   ├── fs.c, fs.h        # 16-slot RAM filesystem
│   ├── sysinfo.c, sysinfo.h  # MIDR/MPIDR/CNT* readers
│   ├── psci.c, psci.h    # PSCI HVC for halt/reset
│   ├── fb.c, fb.h        # ramfb framebuffer + glyph drawing
│   ├── fw_cfg.c, fw_cfg.h  # QEMU fw_cfg DMA client
│   ├── font.c, font.h    # tiny 8×8 bitmap glyphs
│   └── board/
│       ├── qemu-virt.h
│       └── raspi5.h
├── linker/
│   ├── qemu-virt.ld      # load at 0x40000000
│   └── raspi5.ld         # load at 0x80000
└── scripts/
    ├── run-qemu.sh
    └── screenshot.sh
```

## Roadmap

- [ ] GIC + timer interrupts
- [ ] MMU + paging
- [ ] A real heap allocator
- [ ] Cooperative, then pre-emptive scheduler
- [ ] Wake secondary cores via PSCI `CPU_ON`
- [ ] Persistent storage (block device + a real filesystem)
- [ ] User-mode processes
- [ ] Pi 5 framebuffer via the mailbox property channel

## License

MIT.
