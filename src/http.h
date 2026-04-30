#ifndef HOBBY_OS_HTTP_H
#define HOBBY_OS_HTTP_H

#include <stdint.h>

/*
 * Minimal HTTP/1.0 GET. Issues:
 *   GET <path> HTTP/1.0\r\nHost: <host>\r\nConnection: close\r\n\r\n
 * Reads the response until peer closes. Parses status line and
 * skips headers; copies body into out_buf.
 *
 * Returns:
 *   >=0 on success: number of body bytes written into out_buf
 *   -1  : DNS / ARP / connect failure
 *   -2  : send failed
 *   -3  : malformed response
 *   -4  : timeout
 *   -5  : body truncated (would exceed out_max)
 *
 * On success *status_out is populated with the HTTP status code.
 */
int http_get(uint32_t ip, uint16_t port,
             const char *host, const char *path,
             uint8_t *out_buf, uint32_t out_max,
             int *status_out);

#endif
