#include "pkgmgr.h"
#include "str.h"
#include "user_program.h"

typedef struct {
    const char *name;
    const char *summary;
    void      (*entry)(void);
    int        installed;
} pkg_t;

static pkg_t catalog[] = {
    {"hello",   "one-shot greeting from a user task",       user_main_hello,   1},
    {"counter", "prints 1..5 with little pauses",           user_main_counter, 1},
    {"clock",   "tick-tock demo",                            user_main_clock,   0},
    {"load",    "burn some ticks to exercise the scheduler", user_main_load,    0},
    {0, 0, 0, 0},
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
