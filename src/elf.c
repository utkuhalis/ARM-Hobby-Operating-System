#include <stdint.h>
#include "elf.h"
#include "str.h"

#define EI_MAG0   0
#define EI_MAG1   1
#define EI_MAG2   2
#define EI_MAG3   3
#define EI_CLASS  4
#define EI_DATA   5

#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_EXEC 2
#define ET_DYN  3

#define EM_AARCH64 183

#define PT_LOAD 1

struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

static int header_ok(const struct elf64_ehdr *h) {
    return h->e_ident[EI_MAG0] == ELFMAG0
        && h->e_ident[EI_MAG1] == ELFMAG1
        && h->e_ident[EI_MAG2] == ELFMAG2
        && h->e_ident[EI_MAG3] == ELFMAG3
        && h->e_ident[EI_CLASS] == ELFCLASS64
        && h->e_ident[EI_DATA] == ELFDATA2LSB
        && (h->e_type == ET_EXEC || h->e_type == ET_DYN)
        && h->e_machine == EM_AARCH64;
}

int elf_inspect(const void *file, uint64_t file_size, struct elf_image *out) {
    if (!file || file_size < sizeof(struct elf64_ehdr)) return -1;
    const struct elf64_ehdr *h = (const struct elf64_ehdr *)file;
    if (!header_ok(h)) return -2;
    if (h->e_phentsize != sizeof(struct elf64_phdr)) return -3;
    if (h->e_phoff + (uint64_t)h->e_phnum * sizeof(struct elf64_phdr) > file_size) return -4;

    uint64_t lo = ~0ull, hi = 0;
    const struct elf64_phdr *ph =
        (const struct elf64_phdr *)((const uint8_t *)file + h->e_phoff);
    for (uint16_t i = 0; i < h->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint64_t s = ph[i].p_vaddr;
        uint64_t e = s + ph[i].p_memsz;
        if (s < lo) lo = s;
        if (e > hi) hi = e;
    }

    if (out) {
        out->entry       = h->e_entry;
        out->lowest_va   = lo;
        out->highest_va  = hi;
    }
    return 0;
}

int elf_load(const void *file, uint64_t file_size) {
    struct elf_image img;
    int r = elf_inspect(file, file_size, &img);
    if (r != 0) return r;

    const struct elf64_ehdr *h = (const struct elf64_ehdr *)file;
    const struct elf64_phdr *ph =
        (const struct elf64_phdr *)((const uint8_t *)file + h->e_phoff);

    for (uint16_t i = 0; i < h->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_offset + ph[i].p_filesz > file_size) return -5;

        uint8_t *dst = (uint8_t *)(uintptr_t)ph[i].p_vaddr;
        const uint8_t *src = (const uint8_t *)file + ph[i].p_offset;
        memcpy(dst, src, ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz) {
            memset(dst + ph[i].p_filesz, 0,
                   ph[i].p_memsz - ph[i].p_filesz);
        }
    }
    return 0;
}
