// sock_sndbuf.h - read and raise one TCP socket's lwIP send buffer (the tcp_pcb's snd_buf), by its socket fd.
//
// lwIP has no per-socket send-buffer option (no SO_SNDBUF); TCP_SND_BUF is compiled into the core's liblwip.a. These
// two calls reach into lwIP's own state for one socket, under the tcpip core lock. They live in sock_sndbuf.c, a C file
// of its own, so lwIP's private headers stay out of the sketch. The decision (how much, and whether at all) is
// fdSendBufBytes / fdSndBufDelta in fluxdrive_peer.h; this file only reads and writes.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int found;                  // a TCP socket with a pcb: 1; otherwise everything below is 0 except the two defaults
  int established;            // pcb->state == ESTABLISHED
  uint16_t remote_port;       // pcb->remote_port
  uint32_t snd_buf;           // pcb->snd_buf: send buffer free now, in bytes
  uint32_t mss;               // pcb->mss: this connection's maximum segment size
  uint32_t snd_buf_default;   // TCP_SND_BUF of this build (lwIP's default, what a new pcb starts with)
  uint32_t queuelen;          // TCP_SND_QUEUELEN of this build (pbufs a pcb may queue)
} SockTcpInfo;

// The socket's lwIP state, read under the tcpip core lock. fd < 0, no socket, not TCP or no pcb: found = 0.
void sock_tcp_info(int fd, SockTcpInfo *out);

// Adds `delta` to the socket's snd_buf, under the tcpip core lock, only when it is still a TCP socket that is
// ESTABLISHED, talks to remote port `port`, and holds exactly `expect` in snd_buf (what sock_tcp_info read: nothing was
// queued in between), and the sum fits snd_buf. Returns the new snd_buf; 0 = nothing changed.
uint32_t sock_add_sndbuf(int fd, uint16_t port, uint32_t expect, uint32_t delta);

#ifdef __cplusplus
}
#endif
