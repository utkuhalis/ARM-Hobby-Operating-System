#ifndef HOBBY_OS_BROWSER_H
#define HOBBY_OS_BROWSER_H

#include "window.h"

/*
 * Built-in browser: a real HTML/1 renderer (h1/h2/p/a/ul/li/br/hr)
 * over our HTTP/1.0 client. Plain HTTP only -- HTTPS would need a
 * TLS stack which is its own multi-week project.
 */

/* Build / focus the Browser window. */
window_t *browser_window(void);

/* Navigate to a URL. Accepts:
 *   "/path"                    -> default host (10.0.2.2:8090)
 *   "host/path"                -> host on port 80
 *   "host:port/path"
 *   "http://host[:port]/path"
 */
void      browser_navigate(const char *url);

#endif
