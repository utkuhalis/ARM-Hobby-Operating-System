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

#endif
