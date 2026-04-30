#include <stdint.h>
#include "net.h"
#include "virtio_net.h"
#include "str.h"

/*
 * Tiny networking stack: just enough to answer ARP requests and
 * ICMP echo. Not a real TCP/IP stack -- no routing, no fragmentation,
 * no ARP cache aging, no checks against MAC promiscuity. The point is
 * to prove frames go out and come in correctly.
 */

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP4 0x0800

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

#define IP_PROTO_ICMP 1

/* QEMU usermode network: gateway 10.0.2.2, guest gets 10.0.2.15. */
#define DEFAULT_IP ((10u << 24) | (0u << 16) | (2u << 8) | 15u)

static uint32_t my_ip = DEFAULT_IP;
static uint64_t arp_replies, icmp_replies, unhandled;

uint64_t net_arp_replies(void)  { return arp_replies; }
uint64_t net_icmp_replies(void) { return icmp_replies; }
uint64_t net_unhandled(void)    { return unhandled; }
uint32_t net_my_ipv4(void)      { return my_ip; }
void     net_set_ipv4(uint32_t ip) { my_ip = ip; }

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

static void handle_arp(const uint8_t *eth, uint32_t len) {
    if (len < 14 + 28) return;
    const uint8_t *src_mac = eth + 6;
    const uint8_t *arp     = eth + 14;
    uint16_t op = be16(arp + 6);
    if (op != ARP_OP_REQUEST) return;

    uint32_t target_ip = be32(arp + 24);
    if (target_ip != my_ip) return;

    uint8_t reply[14 + 28];
    /* Ethernet: dst = src_mac, src = our_mac, type = ARP */
    for (int i = 0; i < 6; i++) reply[i] = src_mac[i];
    const uint8_t *our = vnet_mac();
    for (int i = 0; i < 6; i++) reply[6 + i] = our[i];
    wbe16(reply + 12, ETH_TYPE_ARP);

    /* ARP body */
    reply[14] = 0; reply[15] = 1;        /* htype = Ethernet */
    reply[16] = 0x08; reply[17] = 0;     /* ptype = IPv4 */
    reply[18] = 6; reply[19] = 4;        /* hlen / plen */
    wbe16(reply + 20, ARP_OP_REPLY);

    for (int i = 0; i < 6; i++) reply[22 + i] = our[i];
    wbe32(reply + 28, my_ip);
    for (int i = 0; i < 6; i++) reply[32 + i] = src_mac[i];
    wbe32(reply + 38, be32(arp + 14)); /* sender protocol addr from request */

    vnet_send(reply, sizeof(reply));
    arp_replies++;
}

static uint16_t ip_csum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2; len -= 2;
    }
    if (len == 1) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static void handle_icmp(const uint8_t *eth, uint32_t len) {
    if (len < 14 + 20 + 8) return;
    const uint8_t *src_mac = eth + 6;
    const uint8_t *ip      = eth + 14;
    if ((ip[0] >> 4) != 4) return;
    uint8_t  ihl    = (ip[0] & 0xf) * 4;
    if (ihl < 20) return;
    uint16_t total  = be16(ip + 2);
    uint8_t  proto  = ip[9];
    uint32_t dst_ip = be32(ip + 16);
    if (proto != IP_PROTO_ICMP) return;
    if (dst_ip != my_ip) return;

    const uint8_t *icmp = ip + ihl;
    uint32_t icmp_len = total - ihl;
    if (icmp_len < 8) return;
    if (icmp[0] != 8) return;  /* not echo request */

    /* Build reply in a static buffer. */
    static uint8_t reply[1500];
    if (total + 14 > sizeof(reply)) return;

    /* Ethernet: dst = src_mac, src = our_mac, type = IP */
    for (int i = 0; i < 6; i++) reply[i] = src_mac[i];
    const uint8_t *our = vnet_mac();
    for (int i = 0; i < 6; i++) reply[6 + i] = our[i];
    wbe16(reply + 12, ETH_TYPE_IP4);

    /* IP header: copy original, swap src/dst, recompute checksum */
    for (uint32_t i = 0; i < ihl; i++) reply[14 + i] = ip[i];
    /* dst <- src, src <- our */
    for (int i = 0; i < 4; i++) reply[14 + 16 + i] = ip[12 + i];
    wbe32(reply + 14 + 12, my_ip);
    /* zero checksum, recompute */
    reply[14 + 10] = 0; reply[14 + 11] = 0;
    uint16_t cs = ip_csum(reply + 14, ihl);
    wbe16(reply + 14 + 10, cs);

    /* ICMP body: type 0 (echo reply), keep id/seq/payload, recompute csum */
    reply[14 + ihl + 0] = 0;
    reply[14 + ihl + 1] = 0;
    reply[14 + ihl + 2] = 0;
    reply[14 + ihl + 3] = 0;
    for (uint32_t i = 4; i < icmp_len; i++) {
        reply[14 + ihl + i] = icmp[i];
    }
    uint16_t icmp_cs = ip_csum(reply + 14 + ihl, icmp_len);
    wbe16(reply + 14 + ihl + 2, icmp_cs);

    vnet_send(reply, 14 + total);
    icmp_replies++;
}

void net_handle_frame(const uint8_t *frame, uint32_t len) {
    if (len < 14) return;

    const uint8_t *our = vnet_mac();
    int dst_for_us = 1;
    for (int i = 0; i < 6; i++) {
        if (frame[i] != our[i] && frame[i] != 0xff) { dst_for_us = 0; break; }
    }
    if (!dst_for_us) {
        /* multicast or other host; the userspace QEMU NAT may still
         * send us things, we just drop them for now */
    }

    uint16_t ethertype = be16(frame + 12);
    if (ethertype == ETH_TYPE_ARP) {
        handle_arp(frame, len);
    } else if (ethertype == ETH_TYPE_IP4) {
        handle_icmp(frame, len);
    } else {
        unhandled++;
    }
}
