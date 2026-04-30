#include <stdint.h>
#include "pkgstore.h"
#include "sha256.h"
#include "str.h"
#include "virtio_blk.h"

/*
 * Disk layout (4 MiB virtio-blk = 8192 sectors of 512 bytes):
 *   sectors    0..255   reserved for the fs (HBFS) region
 *   sectors  256..3327  pkgstore: 12 slots * 256 sectors each
 *   sectors 3328..8191  free / future use
 *
 * Each slot is 256 sectors (128 KiB):
 *   sector 0       : slot header
 *   sectors 1..255 : binary data (up to ~127 KiB)
 */

#define SLOT_BASE        256
#define SLOT_SECTORS     256
#define SLOT_DATA_BASE   1
#define SLOT_NAME_MAX    32

#define SLOT_MAGIC       0x504B4742u  /* "PKGB" */

struct slot_header {
    uint32_t magic;
    uint32_t used;       /* 0 or 1 */
    char     name[SLOT_NAME_MAX];
    uint32_t size;
    uint32_t reserved;
    uint8_t  sha256[32];
    uint8_t  pad[512 - 4 - 4 - SLOT_NAME_MAX - 4 - 4 - 32];
};

static uint8_t io_block[512] __attribute__((aligned(16)));

/* In-memory copy of which slot holds which package, populated by init. */
static struct {
    int       used;
    char      name[SLOT_NAME_MAX];
    uint32_t  size;
} slots[PKGSTORE_SLOT_COUNT];

static uint64_t slot_sector(int idx) {
    return SLOT_BASE + (uint64_t)idx * SLOT_SECTORS;
}

static int read_header(int slot, struct slot_header *out) {
    if (vblk_read(slot_sector(slot), io_block) != 0) return -1;
    const struct slot_header *h = (const struct slot_header *)io_block;
    *out = *h;
    return 0;
}

static int write_header(int slot, const struct slot_header *h) {
    for (int i = 0; i < 512; i++) io_block[i] = 0;
    *(struct slot_header *)io_block = *h;
    return vblk_write(slot_sector(slot), io_block);
}

void pkgstore_init(void) {
    if (!vblk_present()) return;
    for (int i = 0; i < PKGSTORE_SLOT_COUNT; i++) {
        struct slot_header h;
        if (read_header(i, &h) != 0) continue;
        if (h.magic != SLOT_MAGIC || !h.used) {
            slots[i].used = 0;
            slots[i].name[0] = 0;
            slots[i].size = 0;
            continue;
        }
        slots[i].used = 1;
        size_t nl = strlen(h.name);
        if (nl > SLOT_NAME_MAX - 1) nl = SLOT_NAME_MAX - 1;
        memcpy(slots[i].name, h.name, nl);
        slots[i].name[nl] = 0;
        slots[i].size = h.size;
    }
}

static int find_slot(const char *name) {
    for (int i = 0; i < PKGSTORE_SLOT_COUNT; i++) {
        if (slots[i].used && strcmp(slots[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < PKGSTORE_SLOT_COUNT; i++) {
        if (!slots[i].used) return i;
    }
    return -1;
}

int pkgstore_has(const char *name) {
    return find_slot(name) >= 0;
}

int pkgstore_get_size(const char *name, uint32_t *out_size) {
    int s = find_slot(name);
    if (s < 0) return -1;
    if (out_size) *out_size = slots[s].size;
    return 0;
}

int pkgstore_save(const char *name, const void *data, uint32_t size,
                  const uint8_t sha256_expected[32]) {
    if (!vblk_present()) return -1;
    if (size == 0 || size > PKGSTORE_SLOT_DATA_MAX) return -2;
    if (strlen(name) == 0 || strlen(name) >= SLOT_NAME_MAX) return -3;

    /* verify hash before committing anything to disk */
    uint8_t got[32];
    sha256(data, size, got);
    for (int i = 0; i < 32; i++) {
        if (got[i] != sha256_expected[i]) return -4;
    }

    int slot = find_slot(name);
    if (slot < 0) slot = find_free_slot();
    if (slot < 0) return -5;

    /* write data sectors first, header last so a power loss leaves
     * a slot looking unused rather than half-written */
    const uint8_t *src = (const uint8_t *)data;
    uint32_t left = size;
    uint64_t sec  = slot_sector(slot) + SLOT_DATA_BASE;
    while (left > 0) {
        for (int i = 0; i < 512; i++) io_block[i] = 0;
        uint32_t chunk = (left > 512) ? 512 : left;
        memcpy(io_block, src, chunk);
        if (vblk_write(sec, io_block) != 0) return -6;
        src += chunk; left -= chunk; sec += 1;
    }

    struct slot_header h = {0};
    h.magic = SLOT_MAGIC;
    h.used  = 1;
    size_t nl = strlen(name);
    memcpy(h.name, name, nl);
    h.name[nl] = 0;
    h.size = size;
    for (int i = 0; i < 32; i++) h.sha256[i] = sha256_expected[i];
    if (write_header(slot, &h) != 0) return -7;

    /* update in-memory cache */
    slots[slot].used = 1;
    memcpy(slots[slot].name, h.name, nl + 1);
    slots[slot].size = size;
    return 0;
}

int pkgstore_load(const char *name, void *out_buf, uint32_t out_max,
                  uint32_t *out_size) {
    int slot = find_slot(name);
    if (slot < 0) return -1;
    struct slot_header h;
    if (read_header(slot, &h) != 0) return -2;
    if (h.size > out_max) return -3;

    uint8_t *dst = (uint8_t *)out_buf;
    uint32_t left = h.size;
    uint64_t sec  = slot_sector(slot) + SLOT_DATA_BASE;
    while (left > 0) {
        if (vblk_read(sec, io_block) != 0) return -4;
        uint32_t chunk = (left > 512) ? 512 : left;
        memcpy(dst, io_block, chunk);
        dst += chunk; left -= chunk; sec += 1;
    }

    /* Re-verify the SHA recorded in the header to catch disk
     * corruption. */
    uint8_t got[32];
    sha256(out_buf, h.size, got);
    for (int i = 0; i < 32; i++) {
        if (got[i] != h.sha256[i]) return -5;
    }

    if (out_size) *out_size = h.size;
    return (int)h.size;
}

int pkgstore_remove(const char *name) {
    int slot = find_slot(name);
    if (slot < 0) return -1;
    struct slot_header h = {0};
    if (write_header(slot, &h) != 0) return -2;
    slots[slot].used = 0;
    slots[slot].name[0] = 0;
    slots[slot].size = 0;
    return 0;
}

void pkgstore_foreach(void (*cb)(const char *name, void *ctx), void *ctx) {
    for (int i = 0; i < PKGSTORE_SLOT_COUNT; i++) {
        if (slots[i].used) cb(slots[i].name, ctx);
    }
}
