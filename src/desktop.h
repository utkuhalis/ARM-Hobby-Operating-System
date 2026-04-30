#ifndef HOBBY_OS_DESKTOP_H
#define HOBBY_OS_DESKTOP_H

#include <stdint.h>

/*
 * Desktop chrome: a top status bar (clock/date/net) and a bottom
 * dock of installed-package launch icons. Painted on top of every
 * frame and given first crack at pointer events so dock clicks
 * reach pkg_run_by_name.
 */

#define DESKTOP_TOPBAR_H 22
#define DESKTOP_DOCK_H   60

void desktop_init(void);

/* Refresh dock contents (call after pkg install/remove). */
void desktop_rebuild_dock(void);

/* Paint the top bar + dock. Call from window_compose after windows. */
void desktop_paint_chrome(void);

/* Returns 1 if the click landed on a dock icon and was consumed
 * (the package was launched), 0 otherwise. Top bar clicks also
 * consume so they don't bleed through to windows below. */
int  desktop_handle_pointer(int32_t mx, int32_t my,
                            int buttons, int prev_buttons);

#endif
