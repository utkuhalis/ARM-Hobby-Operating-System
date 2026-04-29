#!/usr/bin/env bash
# Boot the qemu-virt kernel image. Press Ctrl-A then X to exit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${ROOT}/build/qemu-virt/kernel.elf"

if [ ! -f "${ELF}" ]; then
    echo "Kernel ELF not found at ${ELF}" >&2
    echo "Run 'make' first." >&2
    exit 1
fi

exec qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a72 \
    -m 128M \
    -nographic \
    -kernel "${ELF}"
