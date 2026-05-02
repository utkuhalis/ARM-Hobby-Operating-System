#include <stdint.h>
#include "dns.h"
#include "net.h"
#include "timer.h"
#include "str.h"
#include "task.h"

/* QEMU slirp publishes a forwarding resolver here. */
#define DNS_SERVER_IP   ((10u<<24) | (0u<<16) | (2u<<8) | 3u)
#define DNS_SERVER_PORT 53

/* One outstanding query. The recv path matches by transaction id +
 * the source port we picked when sending. */
static volatile int      pending;
static volatile uint16_t want_id;
static          uint16_t want_sport;
static volatile int      result_ready;
static volatile int      result_ok;
static volatile uint32_t result_ip;

/* ---- byte helpers ---- */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void wbe16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* Build the QNAME portion of a DNS query: "google.com" ->
 * "\x06google\x03com\x00". Returns bytes written, 0 on overflow. */
static int encode_qname(const char *name, uint8_t *out, int out_max) {
    int o = 0;
    int label_start = o;
    if (o >= out_max) return 0;
    out[o++] = 0;
    int label_len = 0;
    int i = 0;
    while (name[i]) {
        char c = name[i];
        if (c == '.') {
            if (label_len == 0) return 0;  /* empty label */
            out[label_start] = (uint8_t)label_len;
            label_start = o;
            if (o >= out_max) return 0;
            out[o++] = 0;
            label_len = 0;
        } else {
            if (label_len >= 63) return 0;
            if (o >= out_max) return 0;
            out[o++] = (uint8_t)c;
            label_len++;
        }
        i++;
    }
    if (label_len > 0) {
        out[label_start] = (uint8_t)label_len;
    }
    if (o >= out_max) return 0;
    out[o++] = 0;  /* root label */
    return o;
}

/* Walk past a possibly-compressed name. Returns offset of next field
 * after the name, or -1 on malformed input. */
static int skip_name(const uint8_t *pkt, int plen, int off) {
    while (off < plen) {
        uint8_t b = pkt[off];
        if (b == 0) return off + 1;
        if ((b & 0xc0) == 0xc0) {
            if (off + 1 >= plen) return -1;
            return off + 2;   /* compressed pointer; 2 bytes total */
        }
        if (b > 63) return -1;
        off += 1 + b;
    }
    return -1;
}

void dns_recv_packet(uint32_t src_ip, uint16_t sport, uint16_t dport,
                     const uint8_t *payload, uint32_t plen) {
    (void)src_ip; (void)sport;
    if (!pending) return;
    if (dport != want_sport) return;
    if (plen < 12) return;
    uint16_t id = be16(payload + 0);
    if (id != want_id) return;
    uint16_t flags  = be16(payload + 2);
    uint16_t qdcnt  = be16(payload + 4);
    uint16_t ancnt  = be16(payload + 6);
    int rcode = flags & 0xf;

    /* Skip the question section. */
    int off = 12;
    for (int i = 0; i < qdcnt; i++) {
        off = skip_name(payload, (int)plen, off);
        if (off < 0 || off + 4 > (int)plen) { pending = 0; result_ready = 1; result_ok = 0; return; }
        off += 4;  /* QTYPE + QCLASS */
    }

    if (rcode != 0 || ancnt == 0) {
        pending = 0; result_ready = 1; result_ok = 0; return;
    }

    /* Walk answers; pick the first A record (TYPE=1, CLASS=1, RDLEN=4). */
    for (int i = 0; i < ancnt; i++) {
        off = skip_name(payload, (int)plen, off);
        if (off < 0 || off + 10 > (int)plen) break;
        uint16_t type   = be16(payload + off);
        uint16_t klass  = be16(payload + off + 2);
        /* skip ttl (4) */
        uint16_t rdlen  = be16(payload + off + 8);
        off += 10;
        if (off + rdlen > (int)plen) break;
        if (type == 1 && klass == 1 && rdlen == 4) {
            result_ip = be32(payload + off);
            pending = 0;
            result_ready = 1;
            result_ok = 1;
            return;
        }
        off += rdlen;
    }

    pending = 0;
    result_ready = 1;
    result_ok = 0;
}

int dns_lookup(const char *name, uint32_t *out_ip, uint32_t timeout_ticks) {
    if (!name || !out_ip) return -1;

    /* Build query packet */
    uint8_t buf[256];
    uint16_t id = (uint16_t)(timer_ticks() & 0xffff);
    if (id == 0) id = 1;
    wbe16(buf + 0, id);
    wbe16(buf + 2, 0x0100);   /* RD = 1 */
    wbe16(buf + 4, 1);        /* QDCOUNT */
    wbe16(buf + 6, 0);
    wbe16(buf + 8, 0);
    wbe16(buf + 10, 0);
    int qn = encode_qname(name, buf + 12, (int)sizeof(buf) - 12 - 4);
    if (qn <= 0) return -2;
    int off = 12 + qn;
    wbe16(buf + off, 1);      /* QTYPE = A */
    wbe16(buf + off + 2, 1);  /* QCLASS = IN */
    int total = off + 4;

    want_id    = id;
    want_sport = (uint16_t)(0xc000 | ((timer_ticks() ^ 0x9e37) & 0x3fff));
    pending    = 1;
    result_ready = 0;
    result_ok    = 0;

    if (net_send_udp(DNS_SERVER_IP, want_sport, DNS_SERVER_PORT,
                     buf, (uint32_t)total) != 0) {
        pending = 0;
        return -3;
    }

    /* Wait for the response. yield so the compositor + other tasks
     * keep running while we sleep on the network. */
    uint64_t deadline = timer_ticks() + timeout_ticks;
    while (!result_ready) {
        if (timer_ticks() > deadline) {
            pending = 0;
            return -4;
        }
        task_yield();
        __asm__ volatile("wfi");
    }
    if (!result_ok) return -5;
    *out_ip = result_ip;
    return 0;
}
