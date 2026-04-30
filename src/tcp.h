#ifndef HOBBY_OS_TCP_H
#define HOBBY_OS_TCP_H

#include <stdint.h>

/*
 * Single-connection blocking TCP. Just enough to GET a small file
 * over HTTP/1.0 from the local QEMU slirp gateway. Does not handle
 * lost segments, reordering, congestion control, or simultaneous
 * connections.
 */

int  tcp_connect(uint32_t dst_ip, uint16_t dst_port,
                 uint32_t timeout_ticks);

/* Send len bytes; returns bytes accepted or <0 on error. May block
 * waiting for ACK to free the send window. */
int  tcp_send(const void *buf, uint32_t len, uint32_t timeout_ticks);

/* Receive up to maxlen bytes. Returns:
 *   >0  : bytes read
 *    0  : peer closed (FIN received and buffer drained)
 *   -1  : timeout
 *   -2  : connection reset / not connected */
int  tcp_recv(void *buf, uint32_t maxlen, uint32_t timeout_ticks);

void tcp_close(void);

/* Called by net.c when a TCP segment arrives addressed to us. */
void tcp_input(uint32_t src_ip, uint32_t dst_ip,
               const uint8_t *seg, uint32_t seg_len);

/* Diagnostics. */
const char *tcp_state_name(void);

#endif
