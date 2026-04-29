#!/usr/bin/env bash
# Boot the kernel headlessly and capture the framebuffer to PNG.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${ROOT}/build/qemu-virt/kernel.elf"
OUT_DIR="${ROOT}/build"
OUT_PPM="${OUT_DIR}/screenshot.ppm"
OUT_PNG="${OUT_DIR}/screenshot.png"

mkdir -p "${OUT_DIR}"

if [ ! -f "${ELF}" ]; then
    echo "Kernel ELF not found at ${ELF}; run 'make' first." >&2
    exit 1
fi

(
    sleep 3
    echo "screendump ${OUT_PPM}"
    sleep 1
    echo "quit"
) | qemu-system-aarch64 \
        -M virt -cpu cortex-a72 -m 256M \
        -device ramfb \
        -display none \
        -monitor stdio \
        -serial null \
        -kernel "${ELF}" >/dev/null 2>&1 || true

if [ ! -f "${OUT_PPM}" ]; then
    echo "screendump failed: ${OUT_PPM} not produced" >&2
    exit 1
fi

if command -v sips >/dev/null 2>&1; then
    sips -s format png "${OUT_PPM}" --out "${OUT_PNG}" >/dev/null
elif command -v convert >/dev/null 2>&1; then
    convert "${OUT_PPM}" "${OUT_PNG}"
else
    echo "No PPM->PNG converter found (sips or ImageMagick)." >&2
    echo "PPM saved at ${OUT_PPM}." >&2
    exit 1
fi

echo "Screenshot saved to ${OUT_PNG}"
