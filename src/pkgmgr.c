#include "pkgmgr.h"
#include "str.h"
#include "user_program.h"

typedef struct {
    const char *name;
    const char *version;
    const char *summary;
    const char *license;
    int         open_source;
    void      (*entry)(void);
    int         installed;
} pkg_t;

static pkg_t catalog[] = {
    {"hello",   "1.0.0", "one-shot greeting from a user task",
                "MIT",          1, user_main_hello,   1},
    {"counter", "1.0.0", "prints 1..5 with little pauses",
                "MIT",          1, user_main_counter, 1},
    {"clock",   "1.0.0", "tick-tock demo",
                "MIT",          1, user_main_clock,   0},
    {"load",    "1.0.0", "burn some ticks to exercise scheduler",
                "MIT",          1, user_main_load,    0},
    {"notepad", "0.1.0", "tiny notes editor backed by RAM fs",
                "MIT",          1, user_main_notepad, 1},
    {"files",   "0.1.0", "list and dump RAM filesystem contents",
                "MIT",          1, user_main_files,   1},
    {"sysinfo", "0.1.0", "dump CPU + memory + uptime info",
                "MIT",          1, user_main_sysinfo, 1},
    {0, 0, 0, 0, 0, 0, 0},
};

void pkg_init(void) {
    /* nothing to do; install state is in 'catalog' */
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
    return catalog[idx].installed;
}

int pkg_index_of(const char *name) {
    for (int i = 0; catalog[i].name; i++) {
        if (strcmp(catalog[i].name, name) == 0) return i;
    }
    return -1;
}

int pkg_install_by_name(const char *name) {
    int i = pkg_index_of(name);
    if (i < 0) return -1;
    catalog[i].installed = 1;
    return 0;
}

int pkg_remove_by_name(const char *name) {
    int i = pkg_index_of(name);
    if (i < 0) return -1;
    catalog[i].installed = 0;
    return 0;
}

void (*pkg_entry_by_name(const char *name))(void) {
    int i = pkg_index_of(name);
    if (i < 0 || !catalog[i].installed) return 0;
    return catalog[i].entry;
}
