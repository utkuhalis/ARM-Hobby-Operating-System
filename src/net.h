#ifndef HOBBY_OS_NET_H
#define HOBBY_OS_NET_H

#include <stdint.h>

/* Stack-wide statistics. */
uint64_t net_arp_replies(void);
uint64_t net_icmp_replies(void);
uint64_t net_unhandled(void);

/* Get/set our IPv4 address (network byte order). */
uint32_t net_my_ipv4(void);
void     net_set_ipv4(uint32_t ip);

/* Called by the virtio-net driver for every received frame. */
void net_handle_frame(const uint8_t *frame, uint32_t len);

#endif
