#ifndef HOBBY_OS_NET_H
#define HOBBY_OS_NET_H

#include <stdint.h>

/* Stack-wide statistics. */
uint64_t net_arp_replies(void);
uint64_t net_icmp_replies(void);
uint64_t net_unhandled(void);

/* Get/set our IPv4 address (host byte order, big-endian-equivalent uint32). */
uint32_t net_my_ipv4(void);
uint32_t net_gateway_ipv4(void);
void     net_set_ipv4(uint32_t ip);

/* Called by the virtio-net driver for every received frame. */
void net_handle_frame(const uint8_t *frame, uint32_t len);

/* Resolve an IPv4 address to a MAC via ARP. Sends a request if not
 * cached and busy-polls (with WFI) up to timeout_ticks. 0 on success. */
int  net_arp_resolve(uint32_t ip, uint8_t mac_out[6], uint32_t timeout_ticks);

/* Build IPv4+Ethernet headers and transmit. payload is the L4 segment.
 * Routes via gateway for off-link destinations. 0 on success. */
int  net_send_ipv4(uint32_t dst_ip, uint8_t proto,
                   const uint8_t *payload, uint32_t plen);

/* Internet checksum -- exposed so TCP/UDP can compute their own. */
uint16_t net_csum(const uint8_t *data, uint32_t len, uint32_t carry);

/* Build a IPv4 pseudo-header sum suitable for combining with the
 * TCP/UDP segment checksum. Returns the partial sum (NOT folded). */
uint32_t net_pseudo_sum(uint32_t src_ip, uint32_t dst_ip,
                        uint8_t proto, uint16_t seg_len);

#endif
