#!/usr/bin/env bash
# Build a single Hobby ARM OS package.
#
#   tools/sdk/build_pkg.sh <pkgname>
#
# Expects:
#   tools/repo/packages/<pkgname>/manifest.json
#   tools/repo/packages/<pkgname>/source/main.c
#
# Produces:
#   tools/repo/packages/<pkgname>/<pkgname>.elf
#   updates "sha256" and "size" fields in manifest.json
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <pkgname>" >&2
    exit 64
fi

PKG="$1"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SDK="$ROOT/tools/sdk"
PKGDIR="$ROOT/tools/repo/packages/$PKG"
SRC="$PKGDIR/source/main.c"
MANIFEST="$PKGDIR/manifest.json"
ELF="$PKGDIR/$PKG.elf"

if [ ! -f "$SRC" ]; then
    echo "build_pkg: missing $SRC" >&2
    exit 65
fi
if [ ! -f "$MANIFEST" ]; then
    echo "build_pkg: missing $MANIFEST" >&2
    exit 65
fi

CROSS="${CROSS:-aarch64-elf-}"
CC="${CROSS}gcc"

CFLAGS=(
    -ffreestanding -nostdlib -nostartfiles
    -mcpu=cortex-a72 -mgeneral-regs-only
    -fno-pic -fno-stack-protector
    -O2 -Wall -Wextra -std=c11
    -I"$SDK"
)
LDFLAGS=(
    -nostdlib -nostartfiles
    -Wl,-T,"$SDK/user.ld"
    -Wl,--build-id=none
)

echo "[build_pkg] $PKG: $SRC -> $ELF"
"$CC" "${CFLAGS[@]}" "${LDFLAGS[@]}" "$SDK/crt0.S" "$SRC" -o "$ELF"

# Compute size + sha256 in a way that works on both macOS and Linux.
SIZE=$(wc -c < "$ELF" | tr -d ' ')
if command -v sha256sum >/dev/null 2>&1; then
    SHA=$(sha256sum "$ELF" | awk '{print $1}')
else
    SHA=$(shasum -a 256 "$ELF" | awk '{print $1}')
fi

# Patch manifest.json in place. Use python because we already require
# it for the repo container, and it preserves JSON validity.
python3 - "$MANIFEST" "$SHA" "$SIZE" <<'PYEOF'
import json, sys
path, sha, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
with open(path) as f:
    m = json.load(f)
m["sha256"] = sha
m["size"]   = size
with open(path, "w") as f:
    json.dump(m, f, indent=2)
    f.write("\n")
PYEOF

echo "[build_pkg] done: size=$SIZE sha256=$SHA"
