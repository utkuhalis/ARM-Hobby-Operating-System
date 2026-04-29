#include "fs.h"
#include "str.h"

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
