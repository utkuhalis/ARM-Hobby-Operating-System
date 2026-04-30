#ifndef HOBBY_OS_PKGSTORE_H
#define HOBBY_OS_PKGSTORE_H

#include <stdint.h>

/*
 * Persistent blob store for package binaries on virtio-blk. Lives in
 * a region of the disk reserved past the fs area. 12 fixed-size
 * slots, 128 KiB each, addressed by package name. Verified against
 * a stored SHA-256 on read so a corrupted disk can't silently boot
 * a wrong binary.
 */

#define PKGSTORE_SLOT_COUNT     12
#define PKGSTORE_SLOT_DATA_MAX  (255 * 512)  /* ~127 KiB per package */

/* Boot-time scan: builds the in-memory slot table. Idempotent. */
void pkgstore_init(void);

/* True if the named package has a blob on disk. */
int  pkgstore_has(const char *name);

/* Fetch the size of the stored blob; -1 if not present. */
int  pkgstore_get_size(const char *name, uint32_t *out_size);

/* Save a binary blob to disk under `name`. Verifies the supplied
 * SHA-256 against the data before writing. Allocates a fresh slot
 * (or reuses the existing one for `name`). */
int  pkgstore_save(const char *name, const void *data, uint32_t size,
                   const uint8_t sha256[32]);

/* Read the blob into out_buf. Returns bytes read or <0. Re-verifies
 * SHA-256 after reading. */
int  pkgstore_load(const char *name, void *out_buf, uint32_t out_max,
                   uint32_t *out_size);

/* Mark the slot free and zero its header on disk. */
int  pkgstore_remove(const char *name);

/* Iterate installed package names. Calls cb(name, ctx) for each. */
void pkgstore_foreach(void (*cb)(const char *name, void *ctx), void *ctx);

#endif
