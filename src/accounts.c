#include <stdint.h>
#include "accounts.h"
#include "str.h"

#define MAX_ACCOUNTS 8

typedef struct {
    int  used;
    char name[ACCOUNT_NAME_MAX + 1];
    char pass[ACCOUNT_PASS_MAX + 1];
} acct_t;

static acct_t accounts[MAX_ACCOUNTS];
static int    current_idx = -1;

static void cpyn(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void accounts_init(void) {
    for (int i = 0; i < MAX_ACCOUNTS; i++) accounts[i].used = 0;
    /* Default users; login is required at boot. */
    account_add("root",  "root");
    account_add("guest", "guest");
    current_idx = -1;
}

int account_add(const char *name, const char *pass) {
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (accounts[i].used && strcmp(accounts[i].name, name) == 0) {
            return -1; /* already exists */
        }
    }
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (!accounts[i].used) {
            accounts[i].used = 1;
            cpyn(accounts[i].name, name, ACCOUNT_NAME_MAX);
            cpyn(accounts[i].pass, pass, ACCOUNT_PASS_MAX);
            return i;
        }
    }
    return -1;
}

int account_login(const char *name, const char *pass) {
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (accounts[i].used && strcmp(accounts[i].name, name) == 0) {
            if (strcmp(accounts[i].pass, pass) == 0) {
                current_idx = i;
                return 0;
            }
            return -2;
        }
    }
    return -1;
}

void account_logout(void) {
    current_idx = -1;
}

const char *account_current(void) {
    if (current_idx < 0) return "(nobody)";
    return accounts[current_idx].name;
}

int account_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_ACCOUNTS; i++) if (accounts[i].used) n++;
    return n;
}

const char *account_at(int idx) {
    int n = 0;
    for (int i = 0; i < MAX_ACCOUNTS; i++) {
        if (accounts[i].used) {
            if (n == idx) return accounts[i].name;
            n++;
        }
    }
    return 0;
}
