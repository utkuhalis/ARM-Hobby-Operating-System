#ifndef HOBBY_OS_BOOT_SPLASH_H
#define HOBBY_OS_BOOT_SPLASH_H

/*
 * Graphical boot splash. Painted directly to the framebuffer (no
 * window manager) while POST is running, so the user sees a logo
 * and a progress bar instead of an empty black screen.
 */

void boot_splash_init(void);

/* Advance the progress bar to `percent` (0..100) and update the
 * status caption underneath the logo. */
void boot_splash_step(int percent, const char *status);

void boot_splash_done(void);

#endif
