#include <stdint.h>
#include "net.h"
#include "virtio_net.h"
#include "str.h"
#include "timer.h"
#include "tcp.h"
#include "dns.h"

/*
 * Tiny networking stack: ARP, ICMP echo, IPv4 transmit/receive routing,
 * and an ARP cache used by the TCP layer. Not a real stack -- no
 * fragmentation, no routing tables beyond a single gateway, no ARP
 * cache aging. The point is just to talk to the local QEMU slirp.
 */

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP4 0x0800

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

/* QEMU usermode network: gateway 10.0.2.2, guest gets 10.0.2.15. */
#define DEFAULT_IP      ((10u << 24) | (0u << 16) | (2u << 8) | 15u)
#define DEFAULT_GATEWAY ((10u << 24) | (0u << 16) | (2u << 8) |  2u)
#define NETMASK         0xffffff00u  /* /24 */

static uint32_t my_ip   = DEFAULT_IP;
static uint32_t gateway = DEFAULT_GATEWAY;
static uint64_t arp_replies, icmp_replies, unhandled;
static uint16_t ip_id_counter = 1;

uint64_t net_arp_replies(void)  { return arp_replies; }
uint64_t net_icmp_replies(void) { return icmp_replies; }
uint64_t net_unhandled(void)    { return unhandled; }
uint32_t net_my_ipv4(void)      { return my_ip; }
uint32_t net_gateway_ipv4(void) { return gateway; }
void     net_set_ipv4(uint32_t ip) { my_ip = ip; }

/* ------------- byte helpers ------------- */
static uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
static void wbe16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void wbe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t)v;
}

/* ------------- ARP cache ------------- */

#define ARP_MAX 8

struct arp_entry {
    int      valid;
    uint32_t ip;
    uint8_t  mac[6];
};
static struct arp_entry arp_cache[ARP_MAX];

static void arp_cache_put(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            for (int k = 0; k < 6; k++) arp_cache[i].mac[k] = mac[k];
            return;
        }
    }
    for (int i = 0; i < ARP_MAX; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].valid = 1;
            arp_cache[i].ip = ip;
            for (int k = 0; k < 6; k++) arp_cache[i].mac[k] = mac[k];
            return;
        }
    }
    /* Cache full: replace slot 0. */
    arp_cache[0].ip = ip;
    for (int k = 0; k < 6; k++) arp_cache[0].mac[k] = mac[k];
}

static int arp_cache_get(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_MAX; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            for (int k = 0; k < 6; k++) mac_out[k] = arp_cache[i].mac[k];
            return 0;
        }
    }
    return -1;
}

static void send_arp_request(uint32_t target_ip) {
    uint8_t pkt[14 + 28];
    /* dst = broadcast */
    for (int i = 0; i < 6; i++) pkt[i] = 0xff;
    const uint8_t *our = vnet_mac();
    for (int i = 0; i < 6; i++) pkt[6 + i] = our[i];
    wbe16(pkt + 12, ETH_TYPE_ARP);

    pkt[14] = 0; pkt[15] = 1;          /* htype */
    pkt[16] = 0x08; pkt[17] = 0;       /* ptype */
    pkt[18] = 6; pkt[19] = 4;
    wbe16(pkt + 20, ARP_OP_REQUEST);

    for (int i = 0; i < 6; i++) pkt[22 + i] = our[i];
    wbe32(pkt + 28, my_ip);
    for (int i = 0; i < 6; i++) pkt[32 + i] = 0;
    wbe32(pkt + 38, target_ip);

    vnet_send(pkt, sizeof(pkt));
}

int net_arp_resolve(uint32_t ip, uint8_t mac_out[6], uint32_t timeout_ticks) {
    /* Off-link destination -> resolve gateway instead. */
    uint32_t lookup = ((ip & NETMASK) == (my_ip & NETMASK)) ? ip : gateway;

    if (arp_cache_get(lookup, mac_out) == 0) return 0;

    send_arp_request(lookup);
    uint64_t deadline = timer_ticks() + timeout_ticks;
    while (timer_ticks() < deadline) {
        if (arp_cache_get(lookup, mac_out) == 0) return 0;
        __asm__ volatile("wfi");
    }
    /* one last try */
    if (arp_cache_get(lookup, mac_out) == 0) return 0;
    return -1;
}

/* ------------- ARP packet handling ------------- */

static void handle_arp(const uint8_t *eth, uint32_t len) {
    if (len < 14 + 28) return;
    const uint8_t *src_mac = eth + 6;
    const uint8_t *arp     = eth + 14;
    uint16_t op = be16(arp + 6);
    uint32_t sender_ip = be32(arp + 14);

    /* learn sender's mapping regardless of op */
    arp_cache_put(sender_ip, arp + 8);

    if (op == ARP_OP_REPLY) {
        return;
    }
    if (op != ARP_OP_REQUEST) return;

    uint32_t target_ip = be32(arp + 24);
    if (target_ip != my_ip) return;

    uint8_t reply[14 + 28];
    for (int i = 0; i < 6; i++) reply[i] = src_mac[i];
    const uint8_t *our = vnet_mac();
    for (int i = 0; i < 6; i++) reply[6 + i] = our[i];
    wbe16(reply + 12, ETH_TYPE_ARP);

    reply[14] = 0; reply[15] = 1;
    reply[16] = 0x08; reply[17] = 0;
    reply[18] = 6; reply[19] = 4;
    wbe16(reply + 20, ARP_OP_REPLY);

    for (int i = 0; i < 6; i++) reply[22 + i] = our[i];
    wbe32(reply + 28, my_ip);
    for (int i = 0; i < 6; i++) reply[32 + i] = src_mac[i];
    wbe32(reply + 38, sender_ip);

    vnet_send(reply, sizeof(reply));
    arp_replies++;
}

/* ------------- checksum helpers ------------- */

uint16_t net_csum(const uint8_t *data, uint32_t len, uint32_t carry) {
    uint32_t sum = carry;
    while (len > 1) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2; len -= 2;
    }
    if (len == 1) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

uint32_t net_pseudo_sum(uint32_t src_ip, uint32_t dst_ip,
                        uint8_t proto, uint16_t seg_len) {
    uint32_t s = 0;
    s += (src_ip >> 16) & 0xffff;
    s += src_ip & 0xffff;
    s += (dst_ip >> 16) & 0xffff;
    s += dst_ip & 0xffff;
    s += proto;
    s += seg_len;
    return s;
}

/* ------------- ICMP echo ------------- */

static void handle_icmp(const uint8_t *eth, uint32_t len,
                        const uint8_t *ip, uint8_t ihl, uint16_t total) {
    (void)len;
    const uint8_t *src_mac = eth + 6;
    if (total < ihl + 8) return;
    const uint8_t *icmp = ip + ihl;
    uint32_t icmp_len = total - ihl;
    if (icmp[0] != 8) return;  /* not echo request */

    static uint8_t reply[1500];
    if ((uint32_t)total + 14 > sizeof(reply)) return;

    for (int i = 0; i < 6; i++) reply[i] = src_mac[i];
    const uint8_t *our = vnet_mac();
    for (int i = 0; i < 6; i++) reply[6 + i] = our[i];
    wbe16(reply + 12, ETH_TYPE_IP4);

    for (uint32_t i = 0; i < ihl; i++) reply[14 + i] = ip[i];
    for (int i = 0; i < 4; i++) reply[14 + 16 + i] = ip[12 + i];
    wbe32(reply + 14 + 12, my_ip);
    reply[14 + 10] = 0; reply[14 + 11] = 0;
    uint16_t cs = net_csum(reply + 14, ihl, 0);
    wbe16(reply + 14 + 10, cs);

    reply[14 + ihl + 0] = 0;
    reply[14 + ihl + 1] = 0;
    reply[14 + ihl + 2] = 0;
    reply[14 + ihl + 3] = 0;
    for (uint32_t i = 4; i < icmp_len; i++) {
        reply[14 + ihl + i] = icmp[i];
    }
    uint16_t icmp_cs = net_csum(reply + 14 + ihl, icmp_len, 0);
    wbe16(reply + 14 + ihl + 2, icmp_cs);

    vnet_send(reply, 14 + total);
    icmp_replies++;
}

/* ------------- IPv4 dispatch ------------- */

static void handle_ipv4(const uint8_t *eth, uint32_t len) {
    if (len < 14 + 20) return;
    const uint8_t *ip = eth + 14;
    if ((ip[0] >> 4) != 4) return;
    uint8_t  ihl    = (ip[0] & 0xf) * 4;
    if (ihl < 20) return;
    uint16_t total  = be16(ip + 2);
    if (total > len - 14) return;
    uint8_t  proto  = ip[9];
    uint32_t src_ip = be32(ip + 12);
    uint32_t dst_ip = be32(ip + 16);
    if (dst_ip != my_ip) return;

    if (proto == IP_PROTO_ICMP) {
        handle_icmp(eth, len, ip, ihl, total);
        return;
    }
    if (proto == IP_PROTO_TCP) {
        const uint8_t *seg = ip + ihl;
        uint32_t seg_len = total - ihl;
        tcp_input(src_ip, dst_ip, seg, seg_len);
        return;
    }
    if (proto == IP_PROTO_UDP) {
        const uint8_t *seg = ip + ihl;
        uint32_t seg_len = total - ihl;
        if (seg_len < 8) return;
        uint16_t sport = be16(seg + 0);
        uint16_t dport = be16(seg + 2);
        uint16_t ulen  = be16(seg + 4);
        if (ulen < 8 || ulen > seg_len) return;
        dns_recv_packet(src_ip, sport, dport,
                        seg + 8, (uint32_t)(ulen - 8));
        return;
    }
    unhandled++;
}

void net_handle_frame(const uint8_t *frame, uint32_t len) {
    if (len < 14) return;

    uint16_t ethertype = be16(frame + 12);
    if (ethertype == ETH_TYPE_ARP) {
        handle_arp(frame, len);
    } else if (ethertype == ETH_TYPE_IP4) {
        handle_ipv4(frame, len);
    } else {
        unhandled++;
    }
}

/* ------------- IPv4 transmit ------------- */

int net_send_ipv4(uint32_t dst_ip, uint8_t proto,
                  const uint8_t *payload, uint32_t plen) {
    if (plen > 1500 - 20) return -1;

    uint8_t  dst_mac[6];
    if (net_arp_resolve(dst_ip, dst_mac, 250 /* 1 sec @ 250Hz */) != 0) {
        return -2;
    }

    static uint8_t pkt[1600];
    if (14 + 20 + plen > sizeof(pkt)) return -1;

    for (int i = 0; i < 6; i++) pkt[i] = dst_mac[i];
    const uint8_t *our = vnet_mac();
    for (int i = 0; i < 6; i++) pkt[6 + i] = our[i];
    wbe16(pkt + 12, ETH_TYPE_IP4);

    uint8_t *ip = pkt + 14;
    ip[0] = 0x45;                            /* IPv4, IHL=5 */
    ip[1] = 0;                               /* DSCP/ECN */
    wbe16(ip + 2, (uint16_t)(20 + plen));    /* total length */
    wbe16(ip + 4, ip_id_counter++);          /* identification */
    wbe16(ip + 6, 0x4000);                   /* DF, no fragment offset */
    ip[8] = 64;                              /* TTL */
    ip[9] = proto;
    ip[10] = 0; ip[11] = 0;                  /* checksum (computed below) */
    wbe32(ip + 12, my_ip);
    wbe32(ip + 16, dst_ip);
    uint16_t cs = net_csum(ip, 20, 0);
    wbe16(ip + 10, cs);

    for (uint32_t i = 0; i < plen; i++) {
        pkt[14 + 20 + i] = payload[i];
    }
    return vnet_send(pkt, 14 + 20 + plen);
}

int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const uint8_t *payload, uint32_t plen) {
    if (plen > 1500 - 20 - 8) return -1;
    static uint8_t buf[1500];
    /* UDP header (8 B) + payload */
    wbe16(buf + 0, src_port);
    wbe16(buf + 2, dst_port);
    wbe16(buf + 4, (uint16_t)(8 + plen));
    buf[6] = 0; buf[7] = 0;   /* checksum: 0 == disabled (legal for IPv4) */
    for (uint32_t i = 0; i < plen; i++) buf[8 + i] = payload[i];
    return net_send_ipv4(dst_ip, IP_PROTO_UDP, buf, 8 + plen);
}

