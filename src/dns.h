#ifndef HOBBY_OS_DNS_H
#define HOBBY_OS_DNS_H

#include <stdint.h>

/*
 * Tiny DNS resolver. Sends an A-record query over UDP to QEMU slirp's
 * built-in resolver at 10.0.2.3:53 and waits for the response. No
 * caching, no recursion handling -- the slirp resolver does that for
 * us. One outstanding query at a time.
 */

/* Resolve `name` to an IPv4 address (host byte order). Returns 0 on
 * success, <0 on timeout / error. */
int  dns_lookup(const char *name, uint32_t *out_ip, uint32_t timeout_ticks);

/* Net layer hands UDP packets here. Implemented in dns.c. */
void dns_recv_packet(uint32_t src_ip, uint16_t sport, uint16_t dport,
                     const uint8_t *payload, uint32_t plen);

#endif
