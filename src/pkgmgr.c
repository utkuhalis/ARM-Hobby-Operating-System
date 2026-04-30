#include <stdint.h>
#include "pkgmgr.h"
#include "pkgstore.h"
#include "str.h"
#include "user_program.h"
#include "console.h"

#ifdef BOARD_HAS_GIC
#include "http.h"
#include "heap.h"
#include "elf.h"
#include "sha256.h"
#include "task.h"
#endif

/*
 * The package "catalog" is a static list of packages this OS knows
 * about (name + metadata). Whether a given catalog entry is *installed*
 * is determined dynamically by checking pkgstore (the on-disk blob
 * store) -- it is no longer a flag in the catalog itself.
 *
 * Built-in entry points (user_main_*) remain as a development-time
 * fallback so 'run hello' still works before the network/repo are
 * available. Once the ELF loader path lands ('run' off-disk), this
 * fallback can be retired.
 */
typedef struct {
    const char *name;
    const char *version;
    const char *summary;
    const char *license;
    int         open_source;
    void      (*entry)(void);
} pkg_t;

static const pkg_t catalog[] = {
    {"hello",   "1.0.0", "one-shot greeting from a user task",
                "MIT",          1, user_main_hello   },
    {"counter", "1.0.0", "prints 1..5 with little pauses",
                "MIT",          1, user_main_counter },
    {"clock",   "1.0.0", "tick-tock demo",
                "MIT",          1, user_main_clock   },
    {"load",    "1.0.0", "burn some ticks to exercise scheduler",
                "MIT",          1, user_main_load    },
    {"notepad", "0.1.0", "tiny notes editor backed by RAM fs",
                "MIT",          1, user_main_notepad },
    {"files",   "0.1.0", "list and dump RAM filesystem contents",
                "MIT",          1, user_main_files   },
    {"sysinfo", "0.1.0", "dump CPU + memory + uptime info",
                "MIT",          1, user_main_sysinfo },
    {0, 0, 0, 0, 0, 0},
};

void pkg_init(void) {
#ifdef BOARD_HAS_GIC
    pkgstore_init();
#endif
}

static int catalog_size(void) {
    int n = 0;
    while (catalog[n].name) n++;
    return n;
}

int pkg_count(void) { return catalog_size(); }

const char *pkg_name_at(int idx) {
    if (idx < 0 || idx >= catalog_size()) return 0;
    return catalog[idx].name;
}

const char *pkg_summary_at(int idx) {
    if (idx < 0 || idx >= catalog_size()) return 0;
    return catalog[idx].summary;
}

const char *pkg_version_at(int idx) {
    if (idx < 0 || idx >= catalog_size()) return 0;
    return catalog[idx].version;
}

const char *pkg_license_at(int idx) {
    if (idx < 0 || idx >= catalog_size()) return 0;
    return catalog[idx].license;
}

int pkg_open_source_at(int idx) {
    if (idx < 0 || idx >= catalog_size()) return 0;
    return catalog[idx].open_source;
}

int pkg_is_installed(int idx) {
    if (idx < 0 || idx >= catalog_size()) return 0;
#ifdef BOARD_HAS_GIC
    return pkgstore_has(catalog[idx].name);
#else
    return 0;
#endif
}

int pkg_index_of(const char *name) {
    for (int i = 0; catalog[i].name; i++) {
        if (strcmp(catalog[i].name, name) == 0) return i;
    }
    return -1;
}

void (*pkg_entry_by_name(const char *name))(void) {
    int i = pkg_index_of(name);
    if (i < 0) return 0;
#ifdef BOARD_HAS_GIC
    /* prefer the on-disk binary once the real ELF runtime is wired;
     * for now, fall through to the built-in fallback */
#endif
    return catalog[i].entry;
}

#ifdef BOARD_HAS_GIC

/* ---------- tiny JSON value extraction ---------- */

/* Find "key" in a JSON blob and copy its string value into out (max
 * out_max bytes including the NUL). Returns 0 on success. */
static int json_string_field(const char *json, uint32_t json_len,
                             const char *key, char *out, uint32_t out_max) {
    /* search for "key" */
    uint32_t keylen = (uint32_t)strlen(key);
    for (uint32_t i = 0; i + keylen + 2 < json_len; i++) {
        if (json[i] != '"') continue;
        int match = 1;
        for (uint32_t k = 0; k < keylen; k++) {
            if (json[i + 1 + k] != key[k]) { match = 0; break; }
        }
        if (!match) continue;
        if (json[i + 1 + keylen] != '"') continue;
        /* skip past closing quote, whitespace, ':', whitespace */
        uint32_t p = i + 2 + keylen;
        while (p < json_len && (json[p] == ' ' || json[p] == ':')) p++;
        if (p >= json_len || json[p] != '"') continue;
        p++;
        uint32_t o = 0;
        while (p < json_len && json[p] != '"' && o + 1 < out_max) {
            out[o++] = json[p++];
        }
        out[o] = 0;
        return 0;
    }
    return -1;
}

static int json_int_field(const char *json, uint32_t json_len,
                          const char *key, uint32_t *out) {
    uint32_t keylen = (uint32_t)strlen(key);
    for (uint32_t i = 0; i + keylen + 2 < json_len; i++) {
        if (json[i] != '"') continue;
        int match = 1;
        for (uint32_t k = 0; k < keylen; k++) {
            if (json[i + 1 + k] != key[k]) { match = 0; break; }
        }
        if (!match) continue;
        if (json[i + 1 + keylen] != '"') continue;
        uint32_t p = i + 2 + keylen;
        while (p < json_len && (json[p] == ' ' || json[p] == ':')) p++;
        if (p >= json_len) continue;
        uint32_t v = 0;
        int any = 0;
        while (p < json_len && json[p] >= '0' && json[p] <= '9') {
            v = v * 10 + (uint32_t)(json[p] - '0');
            p++; any = 1;
        }
        if (!any) continue;
        *out = v;
        return 0;
    }
    return -1;
}

static int hex_nybble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode_32(const char *hex, uint8_t out[32]) {
    if (strlen(hex) != 64) return -1;
    for (int i = 0; i < 32; i++) {
        int hi = hex_nybble(hex[i*2]);
        int lo = hex_nybble(hex[i*2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* Build a path string into `out`. */
static void make_path(char *out, uint32_t out_max,
                      const char *prefix, const char *name, const char *suffix) {
    uint32_t p = 0;
    while (*prefix && p + 1 < out_max) out[p++] = *prefix++;
    while (*name   && p + 1 < out_max) out[p++] = *name++;
    while (*suffix && p + 1 < out_max) out[p++] = *suffix++;
    out[p] = 0;
}

/* QEMU usermode gateway -> host docker repo at :8090 */
#define REPO_IP   ((10u<<24) | (0u<<16) | (2u<<8) | 2u)
#define REPO_HOST "10.0.2.2"
#define REPO_PORT 8090

int pkg_install_by_name(const char *name) {
    int idx = pkg_index_of(name);
    if (idx < 0) {
        console_printf("install: unknown package '%s'\n", name);
        return -1;
    }

    /* 1. fetch manifest */
    char path[128];
    make_path(path, sizeof(path), "/packages/", name, "/manifest.json");
    console_printf("  GET http://" REPO_HOST ":%d%s\n", REPO_PORT, path);

    static uint8_t manifest[2048];
    int status = 0;
    int n = http_get(REPO_IP, REPO_PORT, REPO_HOST, path,
                     manifest, sizeof(manifest) - 1, &status);
    if (n < 0) {
        console_printf("  install: manifest fetch failed (%d)\n", n);
        return -2;
    }
    manifest[n] = 0;
    if (status != 200) {
        console_printf("  install: manifest HTTP %d\n", status);
        return -3;
    }

    char sha_hex[80];
    uint32_t expected_size = 0;
    if (json_string_field((char *)manifest, (uint32_t)n, "sha256",
                          sha_hex, sizeof(sha_hex)) != 0) {
        console_puts("  install: manifest missing sha256\n");
        return -4;
    }
    if (json_int_field((char *)manifest, (uint32_t)n, "size",
                       &expected_size) != 0) {
        console_puts("  install: manifest missing size\n");
        return -4;
    }
    uint8_t sha_expected[32];
    if (hex_decode_32(sha_hex, sha_expected) != 0) {
        console_puts("  install: bad sha256 hex\n");
        return -4;
    }

    if (expected_size > PKGSTORE_SLOT_DATA_MAX) {
        console_printf("  install: size %u exceeds slot max %u\n",
                       expected_size, (uint32_t)PKGSTORE_SLOT_DATA_MAX);
        return -5;
    }

    /* 2. fetch the .elf into a kalloc'd buffer */
    make_path(path, sizeof(path), "/packages/", name, "/");
    /* path now ends with "/<name>/", need to append "<name>.elf" */
    {
        uint32_t p = (uint32_t)strlen(path);
        const char *n2 = name;
        while (*n2 && p + 1 < sizeof(path)) path[p++] = *n2++;
        const char *suf = ".elf";
        while (*suf && p + 1 < sizeof(path)) path[p++] = *suf++;
        path[p] = 0;
    }
    console_printf("  GET http://" REPO_HOST ":%d%s  (%u bytes)\n",
                   REPO_PORT, path, expected_size);

    uint32_t buf_size = expected_size + 1024;  /* slack for header skip */
    uint8_t *buf = (uint8_t *)kalloc(buf_size);
    if (!buf) {
        console_puts("  install: out of memory\n");
        return -6;
    }
    n = http_get(REPO_IP, REPO_PORT, REPO_HOST, path,
                 buf, buf_size, &status);
    if (n < 0) {
        console_printf("  install: binary fetch failed (%d)\n", n);
        kfree(buf);
        return -7;
    }
    if (status != 200) {
        console_printf("  install: binary HTTP %d\n", status);
        kfree(buf);
        return -8;
    }
    if ((uint32_t)n != expected_size) {
        console_printf("  install: size mismatch (got %d, want %u)\n",
                       n, expected_size);
        kfree(buf);
        return -9;
    }

    /* 3. sanity-check it is a valid AArch64 ELF */
    struct elf_image img;
    if (elf_inspect(buf, n, &img) != 0) {
        console_puts("  install: not a valid AArch64 ELF\n");
        kfree(buf);
        return -10;
    }

    /* 4. persist to pkgstore (which re-verifies sha256 internally) */
    int sr = pkgstore_save(name, buf, n, sha_expected);
    kfree(buf);
    if (sr != 0) {
        console_printf("  install: pkgstore save failed (%d)\n", sr);
        return -11;
    }

    console_printf("  installed %s (%u bytes, entry 0x%lx)\n",
                   name, expected_size, (unsigned long)img.entry);
    return 0;
}

int pkg_remove_by_name(const char *name) {
    int idx = pkg_index_of(name);
    if (idx < 0) return -1;
    if (!pkgstore_has(name)) {
        console_printf("remove: %s is not installed\n", name);
        return -2;
    }
    int r = pkgstore_remove(name);
    if (r != 0) {
        console_printf("remove: pkgstore failed (%d)\n", r);
        return -3;
    }
    console_printf("removed %s\n", name);
    return 0;
}

/* ---------- run a package: prefer the on-disk ELF ---------- */

#define USTACK_SIZE 16384

static int any_user_task_running(void) {
    task_t *t = task_first();
    while (t) {
        if (t->state != TASK_DEAD && t->is_user) return 1;
        t = t->next;
    }
    return 0;
}

static void icache_invalidate_all(void) {
    /* Newly-loaded ELF code may sit in stale I-cache lines from
     * whatever was here before. Push the data side, dump the
     * I-cache, and synchronize. */
    __asm__ volatile(
        "dsb ishst\n"
        "ic iallu\n"
        "dsb ish\n"
        "isb\n"
        ::: "memory"
    );
}

int pkg_run_by_name(const char *name) {
    int idx = pkg_index_of(name);
    if (idx < 0) return -1;

    if (pkgstore_has(name)) {
        if (any_user_task_running()) {
            console_puts("run: another user program is running; "
                         "wait for it to exit first\n");
            return -2;
        }

        uint32_t size = 0;
        pkgstore_get_size(name, &size);
        if (size == 0) return -3;

        uint8_t *buf = (uint8_t *)kalloc(size);
        if (!buf) return -4;
        int n = pkgstore_load(name, buf, size, 0);
        if (n < 0) { kfree(buf); return -5; }

        struct elf_image img;
        if (elf_inspect(buf, n, &img) != 0) { kfree(buf); return -6; }
        if (elf_load(buf, n) != 0)         { kfree(buf); return -7; }
        kfree(buf);
        icache_invalidate_all();

        void *ustack = kalloc(USTACK_SIZE);
        if (!ustack) return -8;

        int id = task_spawn_user(name,
                                 (void (*)(void))(uintptr_t)img.entry,
                                 ustack, USTACK_SIZE);
        if (id < 0) {
            kfree(ustack);
            return -9;
        }
        return id;
    }

    /* Fallback: built-in entry function compiled into the kernel. */
    void (*fn)(void) = catalog[idx].entry;
    if (!fn) return -10;
    return task_spawn(name, (void (*)(void *))fn, 0);
}

#else /* !BOARD_HAS_GIC: no network/disk, fall back to no-op */

int pkg_install_by_name(const char *name) { (void)name; return -1; }
int pkg_remove_by_name(const char *name)  { (void)name; return -1; }
int pkg_run_by_name(const char *name) {
    int i = pkg_index_of(name);
    if (i < 0) return -1;
    return -1;
}

#endif
