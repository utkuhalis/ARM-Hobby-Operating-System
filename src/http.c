#include <stdint.h>
#include "http.h"
#include "tcp.h"
#include "str.h"

/* Append nul-terminated src into dst at offset *off, bounded by max.
 * Returns 0 on overflow. */
static int append(uint8_t *dst, uint32_t *off, uint32_t max, const char *src) {
    while (*src) {
        if (*off >= max) return 0;
        dst[(*off)++] = (uint8_t)*src++;
    }
    return 1;
}

int http_get(uint32_t ip, uint16_t port,
             const char *host, const char *path,
             uint8_t *out_buf, uint32_t out_max,
             int *status_out) {
    if (status_out) *status_out = 0;

    if (tcp_connect(ip, port, 750 /* 3 sec */) != 0) {
        return -1;
    }

    /* Build request. */
    uint8_t req[512];
    uint32_t off = 0;
    if (!append(req, &off, sizeof(req), "GET ")) { tcp_close(); return -2; }
    if (!append(req, &off, sizeof(req), path))   { tcp_close(); return -2; }
    if (!append(req, &off, sizeof(req), " HTTP/1.0\r\nHost: ")) { tcp_close(); return -2; }
    if (!append(req, &off, sizeof(req), host))   { tcp_close(); return -2; }
    if (!append(req, &off, sizeof(req), "\r\nConnection: close\r\n\r\n")) {
        tcp_close(); return -2;
    }

    int s = tcp_send(req, off, 750);
    if (s != (int)off) { tcp_close(); return -2; }

    /* Drain the response into a small ring; parse status line and
     * skip headers, then copy body bytes into out_buf. */
    uint32_t body_len = 0;
    int      header_done = 0;
    int      status_parsed = 0;

    /* Header accumulator: at most one header line at a time. */
    uint8_t  hbuf[512];
    uint32_t hlen = 0;

    /* Scratch for the read */
    uint8_t  buf[1024];

    /* Parse states for header skip: track CRLFCRLF sequence. */
    int    crlf_state = 0;  /* 0=normal, 1=\r, 2=\r\n, 3=\r\n\r */

    for (;;) {
        int n = tcp_recv(buf, sizeof(buf), 750);
        if (n == 0) break;            /* peer closed */
        if (n == -1) { tcp_close(); return -4; }
        if (n < 0)   { tcp_close(); return -3; }

        for (int i = 0; i < n; i++) {
            uint8_t c = buf[i];
            if (!header_done) {
                if (hlen < sizeof(hbuf)) hbuf[hlen++] = c;

                /* Track CRLFCRLF */
                if (crlf_state == 0 && c == '\r') crlf_state = 1;
                else if (crlf_state == 1 && c == '\n') crlf_state = 2;
                else if (crlf_state == 2 && c == '\r') crlf_state = 3;
                else if (crlf_state == 3 && c == '\n') {
                    header_done = 1;
                    crlf_state = 0;
                } else if (c == '\r') crlf_state = 1;
                else crlf_state = 0;

                if (header_done && !status_parsed && hlen > 12) {
                    /* "HTTP/1.x SSS ..." -> parse SSS */
                    if (hbuf[0] == 'H' && hbuf[1] == 'T' && hbuf[2] == 'T' && hbuf[3] == 'P') {
                        int sp = 0;
                        while (sp < (int)hlen && hbuf[sp] != ' ') sp++;
                        if (sp + 3 < (int)hlen) {
                            int code = (hbuf[sp+1]-'0')*100 +
                                       (hbuf[sp+2]-'0')*10  +
                                       (hbuf[sp+3]-'0');
                            if (status_out) *status_out = code;
                            status_parsed = 1;
                        }
                    }
                }
            } else {
                if (body_len >= out_max) { tcp_close(); return -5; }
                out_buf[body_len++] = c;
            }
        }
    }

    tcp_close();
    return (int)body_len;
}
