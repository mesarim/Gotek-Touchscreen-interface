// sock_sndbuf.c - read and raise one TCP socket's lwIP send buffer (snd_buf). See sock_sndbuf.h.
//
// THIS WRITES INTO LWIP'S PRIVATE STATE. lwip_socket_dbg_get_socket is a debug helper declared in a priv/ header; the
// struct tcp_pcb layout is whatever the core's precompiled liblwip.a was built with. The offsets this file uses were
// checked against liblwip.a of esp32 3.3.12 (snd_buf at pcb+176, mss at +126, state at +60, netconn->pcb at +8; only
// tcp_alloc, tcp_write and tcp_receive store to snd_buf, the last adding acked bytes back with no cap). Re-check them
// on every core update, or drop this file: a changed layout compiles cleanly and corrupts TCP state at run time.
#include "sock_sndbuf.h"
#include <string.h>
#include "lwip/opt.h"
#include "lwip/tcpip.h"               // LOCK_TCPIP_CORE / UNLOCK_TCPIP_CORE
#include "lwip/tcp.h"                 // struct tcp_pcb, TCP_SND_BUF, TCP_SND_QUEUELEN, tcpwnd_size_t
#include "lwip/api.h"                 // struct netconn, NETCONNTYPE_GROUP, NETCONN_TCP
#include "lwip/priv/sockets_priv.h"   // lwip_socket_dbg_get_socket, struct lwip_sock

// Without core locking LOCK_TCPIP_CORE() is empty (tcpip.h) and a write from this task would race the tcpip thread.
_Static_assert(LWIP_TCPIP_CORE_LOCKING, "sock_sndbuf.c writes a tcp_pcb from outside the tcpip thread: it needs CONFIG_LWIP_TCPIP_CORE_LOCKING");

// The socket's tcp_pcb, or NULL. Call with the core lock held.
static struct tcp_pcb *pcb_of(int fd) {
  struct lwip_sock *sock = lwip_socket_dbg_get_socket(fd);
  struct netconn *nc = sock ? sock->conn : NULL;
  if (nc == NULL || NETCONNTYPE_GROUP(netconn_type(nc)) != NETCONN_TCP) return NULL;
  return nc->pcb.tcp;
}

void sock_tcp_info(int fd, SockTcpInfo *out) {
  memset(out, 0, sizeof *out);
  out->snd_buf_default = TCP_SND_BUF;
  out->queuelen = TCP_SND_QUEUELEN;
  if (fd < 0) return;
  LOCK_TCPIP_CORE();
  struct tcp_pcb *pcb = pcb_of(fd);
  if (pcb != NULL) {
    out->found = 1;
    out->established = pcb->state == ESTABLISHED;
    out->remote_port = pcb->remote_port;
    out->snd_buf = pcb->snd_buf;
    out->mss = pcb->mss;
  }
  UNLOCK_TCPIP_CORE();
}

uint32_t sock_add_sndbuf(int fd, uint16_t port, uint32_t expect, uint32_t delta) {
  if (fd < 0 || delta == 0) return 0;
  uint32_t now = 0;
  LOCK_TCPIP_CORE();
  struct tcp_pcb *pcb = pcb_of(fd);
  if (pcb != NULL && pcb->state == ESTABLISHED && pcb->remote_port == port && pcb->snd_buf == expect &&
      (uint32_t)pcb->snd_buf + delta <= (uint32_t)(tcpwnd_size_t)-1) {
    pcb->snd_buf = (tcpwnd_size_t)(pcb->snd_buf + delta);
    now = pcb->snd_buf;
  }
  UNLOCK_TCPIP_CORE();
  return now;
}
