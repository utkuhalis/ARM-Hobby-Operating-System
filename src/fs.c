#include "fs.h"
#include "str.h"

#ifdef BOARD_HAS_GIC
#include "virtio_blk.h"
#endif

#define FS_MAGIC  0x48424653u  /* "HBFS" little-endian */
#define FS_BLOCK_START 0       /* lay the fs out from sector 0 */

static fs_file_t files[FS_MAX_FILES];

void fs_init(void) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].name[0] = '\0';
        files[i].size = 0;
    }
}

fs_file_t *fs_find(const char *name) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            return &files[i];
        }
    }
    return 0;
}

fs_file_t *fs_at(int index) {
    if (index < 0 || index >= FS_MAX_FILES) return 0;
    if (!files[index].used) return 0;
    return &files[index];
}

int fs_count(void) {
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) n++;
    }
    return n;
}

static fs_file_t *fs_alloc(void) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) return &files[i];
    }
    return 0;
}

int fs_write(const char *name, const void *data, uint32_t size) {
    if (size > FS_MAX_DATA) return -1;
    if (strlen(name) > FS_MAX_NAME) return -1;
    if (strlen(name) == 0) return -1;

    fs_file_t *f = fs_find(name);
    if (!f) {
        f = fs_alloc();
        if (!f) return -1;
        f->used = 1;
        size_t nl = strlen(name);
        memcpy(f->name, name, nl);
        f->name[nl] = '\0';
    }
    if (size > 0) memcpy(f->data, data, size);
    f->size = size;
    return 0;
}

int fs_delete(const char *name) {
    fs_file_t *f = fs_find(name);
    if (!f) return -1;
    f->used = 0;
    f->name[0] = '\0';
    f->size = 0;
    return 0;
}

#ifdef BOARD_HAS_GIC

#define FS_BYTES (sizeof(files))
#define FS_SECTORS ((FS_BYTES + 511) / 512)

struct fs_super {
    uint32_t magic;
    uint32_t version;
    uint32_t file_count;
    uint32_t reserved;
};

static uint8_t io_block[512] __attribute__((aligned(16)));

int fs_save(void) {
    if (!vblk_present()) return -1;

    /* Sector 0: superblock */
    struct fs_super *sb = (struct fs_super *)io_block;
    for (int i = 0; i < 512; i++) io_block[i] = 0;
    sb->magic       = FS_MAGIC;
    sb->version     = 1;
    sb->file_count  = (uint32_t)fs_count();
    if (vblk_write(FS_BLOCK_START, io_block) != 0) return -2;

    /* Sectors 1..N: file table */
    const uint8_t *src = (const uint8_t *)files;
    uint32_t left = FS_BYTES;
    uint64_t sec = FS_BLOCK_START + 1;
    while (left > 0) {
        uint32_t chunk = (left > 512) ? 512 : left;
        for (int i = 0; i < 512; i++) io_block[i] = 0;
        memcpy(io_block, src, chunk);
        if (vblk_write(sec, io_block) != 0) return -3;
        src  += chunk;
        left -= chunk;
        sec  += 1;
    }
    return 0;
}

int fs_load(void) {
    if (!vblk_present()) return -1;

    if (vblk_read(FS_BLOCK_START, io_block) != 0) return -2;
    struct fs_super *sb = (struct fs_super *)io_block;
    if (sb->magic != FS_MAGIC) return -3;

    uint8_t *dst = (uint8_t *)files;
    uint32_t left = FS_BYTES;
    uint64_t sec = FS_BLOCK_START + 1;
    while (left > 0) {
        uint32_t chunk = (left > 512) ? 512 : left;
        if (vblk_read(sec, io_block) != 0) return -4;
        memcpy(dst, io_block, chunk);
        dst  += chunk;
        left -= chunk;
        sec  += 1;
    }
    return 0;
}

#endif /* BOARD_HAS_GIC */
