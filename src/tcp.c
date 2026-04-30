#include <stdint.h>
#include "tcp.h"
#include "net.h"
#include "timer.h"
#include "str.h"

/*
 * Single-connection blocking TCP. Built for talking to QEMU's slirp
 * gateway (no packet loss, in-order delivery), so we cheat hard:
 *  - no retransmission timers
 *  - no out-of-order queue (drop, ACK current rcv_nxt)
 *  - no Nagle, no PSH coalescing
 *  - no MSS option (we just emit small enough segments)
 *  - one connection at a time
 */

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_PROTO 6

#define RX_BUF_SIZE  16384
#define MSS          1300

typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK,
} tcp_state_t;

static tcp_state_t state = TCP_CLOSED;
static uint32_t  peer_ip;
static uint16_t  peer_port;
static uint16_t  local_port;

static uint32_t  snd_iss;        /* initial sequence we sent */
static uint32_t  snd_nxt;        /* next seq we'll send */
static uint32_t  snd_una;        /* oldest unacknowledged seq */
static uint32_t  rcv_nxt;        /* next seq we expect from peer */

static uint8_t   rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_rd, rx_wr;  /* circular buffer indices */
static volatile int      peer_fin;       /* peer sent FIN */
static volatile int      reset_seen;     /* peer sent RST */

const char *tcp_state_name(void) {
    switch (state) {
    case TCP_CLOSED:      return "CLOSED";
    case TCP_SYN_SENT:    return "SYN_SENT";
    case TCP_ESTABLISHED: return "ESTABLISHED";
    case TCP_FIN_WAIT_1:  return "FIN_WAIT_1";
    case TCP_FIN_WAIT_2:  return "FIN_WAIT_2";
    case TCP_CLOSE_WAIT:  return "CLOSE_WAIT";
    case TCP_LAST_ACK:    return "LAST_ACK";
    }
    return "?";
}

/* ------------- byte helpers ------------- */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void wbe16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wbe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}

/* ------------- circular buffer ------------- */

static uint32_t rx_avail(void) {
    return rx_wr - rx_rd;
}
static void rx_push(const uint8_t *data, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (rx_avail() >= RX_BUF_SIZE) return;  /* drop overflow */
        rx_buf[(rx_wr++) % RX_BUF_SIZE] = data[i];
    }
}
static uint32_t rx_pop(uint8_t *out, uint32_t maxlen) {
    uint32_t a = rx_avail();
    if (a > maxlen) a = maxlen;
    for (uint32_t i = 0; i < a; i++) out[i] = rx_buf[(rx_rd++) % RX_BUF_SIZE];
    return a;
}

/* ------------- segment build/send ------------- */

static int send_segment(uint8_t flags, const uint8_t *data, uint32_t dlen) {
    if (dlen > 1500 - 20 - 20) return -1;  /* IP+TCP headers */

    uint8_t pkt[20 + 1500];
    uint8_t *tcp = pkt;
    /* TCP header: src_port, dst_port, seq, ack, off|flags, win, csum, urg */
    wbe16(tcp + 0, local_port);
    wbe16(tcp + 2, peer_port);
    wbe32(tcp + 4, snd_nxt);
    wbe32(tcp + 8, rcv_nxt);
    tcp[12] = 0x50;          /* data offset = 5 (20 bytes), reserved=0 */
    tcp[13] = flags;
    wbe16(tcp + 14, 0xffff); /* window: max */
    tcp[16] = 0; tcp[17] = 0;
    wbe16(tcp + 18, 0);      /* urg ptr */

    for (uint32_t i = 0; i < dlen; i++) tcp[20 + i] = data[i];

    /* checksum: pseudo header + tcp header + data */
    uint32_t carry = net_pseudo_sum(net_my_ipv4(), peer_ip,
                                    TCP_PROTO, (uint16_t)(20 + dlen));
    uint16_t cs = net_csum(tcp, 20 + dlen, carry);
    wbe16(tcp + 16, cs);

    /* advance snd_nxt for SYN/FIN (each consumes 1 seq) and data */
    if (flags & TCP_FLAG_SYN) snd_nxt += 1;
    if (flags & TCP_FLAG_FIN) snd_nxt += 1;
    snd_nxt += dlen;

    return net_send_ipv4(peer_ip, TCP_PROTO, tcp, 20 + dlen);
}

/* Send a pure ACK without advancing snd_nxt. */
static void send_ack(void) {
    uint8_t pkt[20];
    wbe16(pkt + 0, local_port);
    wbe16(pkt + 2, peer_port);
    wbe32(pkt + 4, snd_nxt);
    wbe32(pkt + 8, rcv_nxt);
    pkt[12] = 0x50;
    pkt[13] = TCP_FLAG_ACK;
    wbe16(pkt + 14, 0xffff);
    pkt[16] = 0; pkt[17] = 0;
    wbe16(pkt + 18, 0);

    uint32_t carry = net_pseudo_sum(net_my_ipv4(), peer_ip, TCP_PROTO, 20);
    uint16_t cs = net_csum(pkt, 20, carry);
    wbe16(pkt + 16, cs);

    net_send_ipv4(peer_ip, TCP_PROTO, pkt, 20);
}

/* ------------- public API ------------- */

int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint32_t timeout_ticks) {
    if (state != TCP_CLOSED) return -1;

    peer_ip   = dst_ip;
    peer_port = dst_port;
    /* pseudo-random local port from timer; 49152..65535 */
    local_port = (uint16_t)(0xc000 | (timer_ticks() & 0x3fff));

    snd_iss = (uint32_t)(timer_ticks() * 0x9e3779b1u);
    snd_nxt = snd_iss;
    snd_una = snd_iss;
    rcv_nxt = 0;

    rx_rd = rx_wr = 0;
    peer_fin = 0;
    reset_seen = 0;

    state = TCP_SYN_SENT;
    if (send_segment(TCP_FLAG_SYN, 0, 0) != 0) {
        state = TCP_CLOSED;
        return -2;
    }

    uint64_t deadline = timer_ticks() + timeout_ticks;
    while (state == TCP_SYN_SENT) {
        if (reset_seen) { state = TCP_CLOSED; return -3; }
        if (timer_ticks() > deadline) { state = TCP_CLOSED; return -4; }
        __asm__ volatile("wfi");
    }
    return state == TCP_ESTABLISHED ? 0 : -5;
}

int tcp_send(const void *buf, uint32_t len, uint32_t timeout_ticks) {
    if (state != TCP_ESTABLISHED && state != TCP_CLOSE_WAIT) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t sent = 0;

    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > MSS) chunk = MSS;

        /* wait for previous segment to be ACK'd before sending the
         * next one (cwnd of 1) */
        uint64_t deadline = timer_ticks() + timeout_ticks;
        while (snd_nxt != snd_una) {
            if (reset_seen) return -2;
            if (timer_ticks() > deadline) return (int)sent;
            __asm__ volatile("wfi");
        }

        if (send_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, p + sent, chunk) != 0) {
            return (int)sent;
        }
        sent += chunk;
    }
    return (int)sent;
}

int tcp_recv(void *buf, uint32_t maxlen, uint32_t timeout_ticks) {
    if (state == TCP_CLOSED) return -2;
    uint64_t deadline = timer_ticks() + timeout_ticks;
    while (rx_avail() == 0) {
        if (reset_seen) return -2;
        if (peer_fin) return 0;
        if (state == TCP_CLOSED) return -2;
        if (timer_ticks() > deadline) return -1;
        __asm__ volatile("wfi");
    }
    return (int)rx_pop((uint8_t *)buf, maxlen);
}

void tcp_close(void) {
    if (state == TCP_ESTABLISHED) {
        send_segment(TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0);
        state = TCP_FIN_WAIT_1;
    } else if (state == TCP_CLOSE_WAIT) {
        send_segment(TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0);
        state = TCP_LAST_ACK;
    } else {
        state = TCP_CLOSED;
        return;
    }

    /* wait briefly for the final ACK; we don't really care if it's
     * lost since the connection is one-shot */
    uint64_t deadline = timer_ticks() + 250;  /* 1 sec */
    while (state != TCP_CLOSED) {
        if (timer_ticks() > deadline) break;
        __asm__ volatile("wfi");
    }
    state = TCP_CLOSED;
}

/* ------------- input path (called from IRQ via net.c) ------------- */

void tcp_input(uint32_t src_ip, uint32_t dst_ip,
               const uint8_t *seg, uint32_t seg_len) {
    (void)dst_ip;
    if (state == TCP_CLOSED) return;
    if (seg_len < 20) return;

    uint16_t sport = be16(seg + 0);
    uint16_t dport = be16(seg + 2);
    uint32_t seq   = be32(seg + 4);
    uint32_t ack   = be32(seg + 8);
    uint8_t  off   = (seg[12] >> 4) * 4;
    uint8_t  flags = seg[13];
    if (off < 20 || off > seg_len) return;
    if (sport != peer_port || dport != local_port) return;
    if (src_ip != peer_ip) return;

    const uint8_t *data = seg + off;
    uint32_t      dlen  = seg_len - off;

    if (flags & TCP_FLAG_RST) { reset_seen = 1; state = TCP_CLOSED; return; }

    if (state == TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)
            && ack == snd_nxt) {
            snd_una = ack;
            rcv_nxt = seq + 1;
            state = TCP_ESTABLISHED;
            send_ack();
            return;
        }
        return;
    }

    /* Common: ACK processing for everyone past SYN_SENT. */
    if (flags & TCP_FLAG_ACK) {
        /* Accept any ACK in (snd_una, snd_nxt]. */
        if ((int32_t)(ack - snd_una) > 0 && (int32_t)(ack - snd_nxt) <= 0) {
            snd_una = ack;
        }
    }

    /* Data delivery: only accept seg_seq == rcv_nxt. */
    if (dlen > 0 && seq == rcv_nxt) {
        rx_push(data, dlen);
        rcv_nxt += dlen;
        send_ack();
    } else if (dlen > 0) {
        /* duplicate or out-of-order: re-ack our current rcv_nxt */
        send_ack();
    }

    /* FIN handling. The FIN consumes one seq and arrives with seq
     * equal to (rcv_nxt + dlen) in the segment we just processed. */
    if (flags & TCP_FLAG_FIN) {
        if (seq + dlen == rcv_nxt) {
            rcv_nxt += 1;
            peer_fin = 1;
            send_ack();
            switch (state) {
            case TCP_ESTABLISHED: state = TCP_CLOSE_WAIT; break;
            case TCP_FIN_WAIT_1:
            case TCP_FIN_WAIT_2:  state = TCP_CLOSED; break;
            default: break;
            }
        }
    }

    /* If we're waiting for the ACK of our FIN... */
    if (state == TCP_FIN_WAIT_1 && snd_una == snd_nxt) {
        state = peer_fin ? TCP_CLOSED : TCP_FIN_WAIT_2;
    }
    if (state == TCP_LAST_ACK && snd_una == snd_nxt) {
        state = TCP_CLOSED;
    }
}
