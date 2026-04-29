#ifndef HOBBY_OS_ACCOUNTS_H
#define HOBBY_OS_ACCOUNTS_H

#include <stdint.h>

#define ACCOUNT_NAME_MAX 16
#define ACCOUNT_PASS_MAX 32

void accounts_init(void);
int  account_login(const char *name, const char *pass);
void account_logout(void);
const char *account_current(void);

int  account_add(const char *name, const char *pass);
int  account_count(void);
const char *account_at(int idx);

#endif
