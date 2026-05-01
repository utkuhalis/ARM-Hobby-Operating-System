#ifndef HOBBY_OS_WALLPAPER_H
#define HOBBY_OS_WALLPAPER_H

#include <stdint.h>

/*
 * Desktop wallpaper. The compositor calls wallpaper_paint() at the
 * start of every frame instead of fb_clear(). Choices are baked into
 * the kernel (no external image format yet) and the active index
 * persists in /etc/wallpaper so the choice survives reboot.
 */

int          wallpaper_count(void);
const char  *wallpaper_name(int idx);

/* Currently active index. */
int          wallpaper_get(void);
int          wallpaper_set(int idx);

/* Restore the last-saved choice from fs. Called once at boot. */
void         wallpaper_load(void);

void         wallpaper_paint(void);

#endif
