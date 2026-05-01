#ifndef HOBBY_OS_MAKER_H
#define HOBBY_OS_MAKER_H

#include "window.h"

/*
 * Maker: a tiny block-style programming environment that runs inside
 * Hobby ARM OS. The user composes a program by clicking blocks in
 * the palette (Print / Set / If / Repeat / End), tweaks each block's
 * parameter via the inline editor, hits Run to execute, Save to
 * persist as JSON in the local fs, and Upload to publish to the
 * marketplace.
 */

window_t *maker_window(void);

#endif
