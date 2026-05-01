#ifndef HOBBY_OS_FS_H
#define HOBBY_OS_FS_H

#include <stdint.h>

#define FS_MAX_FILES 16
#define FS_MAX_NAME  31
#define FS_MAX_DATA  4096

typedef struct fs_file {
    int      used;
    char     name[FS_MAX_NAME + 1];
    uint32_t size;
    uint8_t  data[FS_MAX_DATA];
} fs_file_t;

void  fs_init(void);

fs_file_t *fs_find(const char *name);
fs_file_t *fs_at(int index);
int   fs_count(void);

int   fs_write(const char *name, const void *data, uint32_t size);
int   fs_delete(const char *name);

int   fs_save(void);  /* persist current state to virtio-blk */
int   fs_load(void);  /* hydrate from virtio-blk; -1 if magic missing */

/* Toggle automatic fs_save() after every fs_write / fs_delete. Off
 * during the seed-on-boot phase to avoid pointless disk traffic;
 * the kernel turns it on once boot is complete. */
void  fs_set_autosave(int on);

#endif
