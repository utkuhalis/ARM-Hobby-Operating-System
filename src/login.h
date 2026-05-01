#ifndef HOBBY_OS_LOGIN_H
#define HOBBY_OS_LOGIN_H

/*
 * Full-screen login gate. Paints a centered card with username and
 * password fields, polls keyboard + mouse, and only returns once
 * accounts.c accepts the credentials. Called from kernel_main before
 * the desktop chrome and shell are set up.
 */
void login_run(void);

#endif
