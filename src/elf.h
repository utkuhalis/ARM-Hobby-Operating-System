#ifndef HOBBY_OS_ELF_H
#define HOBBY_OS_ELF_H

#include <stdint.h>

/*
 * Tiny AArch64 ELF64 loader. Validates the header, walks PT_LOAD
 * program headers and copies their bytes into RAM at the addresses
 * the file asks for. Caller is responsible for allocating space and
 * for jumping to the entry point.
 */

struct elf_image {
    uint64_t entry;
    uint64_t lowest_va;
    uint64_t highest_va;
};

int elf_inspect(const void *file, uint64_t file_size, struct elf_image *out);
int elf_load  (const void *file, uint64_t file_size);

#endif
