#ifndef HOBBY_OS_PKGMGR_H
#define HOBBY_OS_PKGMGR_H

void  pkg_init(void);
int   pkg_count(void);
const char *pkg_name_at(int idx);
const char *pkg_summary_at(int idx);
int   pkg_is_installed(int idx);
int   pkg_index_of(const char *name);
int   pkg_install_by_name(const char *name);
int   pkg_remove_by_name(const char *name);
void  (*pkg_entry_by_name(const char *name))(void);

#endif
