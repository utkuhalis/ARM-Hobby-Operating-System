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
    -M virt,gic-version=2 \
    -cpu cortex-a72 \
    -m 256M \
    -global virtio-mmio.force-legacy=false \
    -device ramfb \
    -device virtio-keyboard-device \
    -device virtio-tablet-device \
    -nographic \
    -kernel "${ELF}"
