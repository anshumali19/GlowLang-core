/* websocket.c: WebSocket connections (RFC 6455), client and server.

   ws_native.connect(url, options)      -> connection id   (ws:// or wss://)
   ws_native.accept(socket, request, options) -> connection id
   ws_native.send(id, data, binary)
   ws_native.recv(id, timeout)          -> {"type": "text"|"binary"|"close", ...} or none
   ws_native.ping(id, data)
   ws_native.close(id, code, reason)
   ws_native.info(id)                   -> {"open", "protocol", "close_code", "close_reason"}

   lib/websocket.glow wraps these in a WebSocket object.

   Threads: one thread may receive while others send. Frames are written
   whole under a write lock, so messages never interleave. With TLS, every
   SSL call is made under an I/O lock and waits (poll) happen outside it,
   so a reader never blocks a writer for long. The GIL is released while
   waiting. No extensions (permessage-deflate) are negotiated. */
#include "glowlang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET ws_sock_t;
#define WS_BAD_SOCK INVALID_SOCKET
#define ws_closesock closesocket
#define WS_EAGAIN(e) ((e) == WSAEWOULDBLOCK)
#define WS_EINTR(e) ((e) == WSAEINTR)
#define ws_sockerr() WSAGetLastError()
#define ws_poll WSAPoll
#define WS_SHUT_BOTH SD_BOTH
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <pthread.h>
typedef int ws_sock_t;
#define WS_BAD_SOCK (-1)
#define ws_closesock close
#define WS_EAGAIN(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#define WS_EINTR(e) ((e) == EINTR)
#define ws_sockerr() errno
#define ws_poll poll
#define WS_SHUT_BOTH SHUT_RDWR
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

/* From stdlib.c: the HTTP client's connect (with a timeout) and its TLS
   context, which trusts the system's root certificates. */
intptr_t glow_net_connect(const char *host, int port, double seconds, char *err, size_t errsz);
void *glow_tls_client_ctx(void);

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_DEFAULT_MAX (16u * 1024u * 1024u)   /* largest message, by default */
#define WS_MAX_HEAD (16 * 1024)                /* largest handshake */
#define WS_SLOTS 65536

enum { WS_OPEN = 0, WS_CLOSED = 2 };

typedef struct WsConn {
    int64_t id;
    ws_sock_t sock;
    SSL *ssl;
    int is_client;                 /* clients mask what they send */
    pthread_mutex_t wlock;         /* one frame written at a time */
    pthread_mutex_t io;            /* every SSL call (TLS only) */
    pthread_mutex_t rlock;         /* one receiver at a time */
    pthread_mutex_t state_lock;    /* refs, state, close fields */
    int refs;
    int state;
    int in_table;
    int close_sent;
    int write_broken;              /* a send failed: no more sending */
    int close_received;
    int close_code;                /* 0 until closed */
    double close_deadline;         /* after our close: give up waiting then */
    char close_reason[128];
    unsigned char *rbuf; size_t rlen, rcap;      /* read, not yet parsed */
    unsigned char *msg; size_t mlen, mcap;       /* message being assembled */
    int msg_op;                    /* 0 none, 1 text, 2 binary */
    int utf8_need, utf8_lo, utf8_hi;             /* incremental UTF-8 check */
    size_t max_message;
    double send_timeout;
    char protocol[128];
} WsConn;

static pthread_mutex_t ws_table_lock = PTHREAD_MUTEX_INITIALIZER;
static WsConn *ws_table[WS_SLOTS];
static uint32_t ws_gen[WS_SLOTS];
static int ws_next_slot = 0;

static double ws_now(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 0)))
#endif
static Value ws_fail(const char *fmt, const char *a) {
    char msg[512];
    snprintf(msg, sizeof(msg), fmt, a ? a : "");
    pending_exception = value_string_copy(msg);
    has_exception = 1;
    return value_none();
}

/* ---- connection table ---- */

static void ws_mutex_init(pthread_mutex_t *m) {
#ifdef _WIN32
    InitializeSRWLock(m);
#else
    pthread_mutex_init(m, NULL);
#endif
}

static void ws_mutex_destroy(pthread_mutex_t *m) {
#ifndef _WIN32
    pthread_mutex_destroy(m);
#else
    (void)m;
#endif
}

static int ws_mutex_trylock(pthread_mutex_t *m) {
#ifdef _WIN32
    return TryAcquireSRWLockExclusive(m) ? 0 : 1;
#else
    return pthread_mutex_trylock(m);
#endif
}

static WsConn *ws_conn_new(ws_sock_t s, SSL *ssl, int is_client) {
    WsConn *c = calloc(1, sizeof(WsConn));
    if (!c) return NULL;
    c->sock = s; c->ssl = ssl; c->is_client = is_client;
    ws_mutex_init(&c->wlock); ws_mutex_init(&c->io); ws_mutex_init(&c->rlock); ws_mutex_init(&c->state_lock);
    c->max_message = WS_DEFAULT_MAX;
    c->send_timeout = 30;
    c->refs = 1;                   /* the table's */
    return c;
}

static void ws_conn_free(WsConn *c) {
    if (c->ssl) SSL_free(c->ssl);
    if (c->sock != WS_BAD_SOCK) ws_closesock(c->sock);
    free(c->rbuf); free(c->msg);
    ws_mutex_destroy(&c->wlock); ws_mutex_destroy(&c->io); ws_mutex_destroy(&c->rlock); ws_mutex_destroy(&c->state_lock);
    free(c);
}

/* Adds c to the table; its id, or -1 when the table is full. */
static int64_t ws_register(WsConn *c) {
    pthread_mutex_lock(&ws_table_lock);
    for (int k = 0; k < WS_SLOTS; k++) {
        int slot = (ws_next_slot + k) % WS_SLOTS;
        if (slot == 0) continue;               /* ids are never 0 */
        if (!ws_table[slot]) {
            ws_gen[slot] = (ws_gen[slot] + 1) & 0x7FFFFF;
            c->id = ((int64_t)ws_gen[slot] << 16) | slot;
            c->in_table = 1;
            ws_table[slot] = c;
            ws_next_slot = slot + 1;
            pthread_mutex_unlock(&ws_table_lock);
            return c->id;
        }
    }
    pthread_mutex_unlock(&ws_table_lock);
    return -1;
}

/* The connection with this id, with a reference the caller must release. */
static WsConn *ws_get(int64_t id) {
    if (id <= 0) return NULL;
    int slot = (int)(id & 0xFFFF);
    WsConn *c = NULL;
    pthread_mutex_lock(&ws_table_lock);
    if (ws_table[slot] && ws_table[slot]->id == id) {
        c = ws_table[slot];
        pthread_mutex_lock(&c->state_lock); c->refs++; pthread_mutex_unlock(&c->state_lock);
    }
    pthread_mutex_unlock(&ws_table_lock);
    return c;
}

static void ws_release(WsConn *c) {
    pthread_mutex_lock(&c->state_lock);
    int last = --c->refs == 0;
    pthread_mutex_unlock(&c->state_lock);
    if (last) ws_conn_free(c);
}

/* Closes the connection for good: wakes any thread waiting on the socket,
   takes it out of the table and drops the table's reference. The socket
   itself is closed when the last user lets go, so its number cannot be
   reused while another thread still polls it. */
static void ws_teardown(WsConn *c) {
    pthread_mutex_lock(&c->state_lock);
    int was_open = c->state != WS_CLOSED;
    c->state = WS_CLOSED;
    pthread_mutex_unlock(&c->state_lock);
    if (!was_open) return;
    shutdown(c->sock, WS_SHUT_BOTH);
    pthread_mutex_lock(&ws_table_lock);
    int slot = (int)(c->id & 0xFFFF);
    int drop = c->in_table && ws_table[slot] == c;
    if (drop) { ws_table[slot] = NULL; c->in_table = 0; }
    pthread_mutex_unlock(&ws_table_lock);
    if (drop) ws_release(c);
}

/* ---- waiting and raw I/O ---- */

static void ws_nonblocking(ws_sock_t s) {
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
    int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
#ifdef _WIN32
    BOOL one = TRUE; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    /* no timeouts on the socket itself: waits are done with poll */
    DWORD zero = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&zero, sizeof(zero));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&zero, sizeof(zero));
#else
    struct timeval tv = {0, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* Waits until the socket can be read (or written). 1 ready, 0 timed out,
   -1 error. deadline < 0 waits forever. */
static int ws_wait(WsConn *c, int for_write, double deadline) {
    ws_sock_t s = c->sock;
    for (;;) {
        if (c->state == WS_CLOSED) return -1;
        if (c->close_deadline > 0 && ws_now() > c->close_deadline) return -1;
        int ms = -1;
        if (deadline >= 0) {
            double left = deadline - ws_now();
            if (left <= 0) return 0;
            ms = (int)(left * 1000) + 1;
            if (ms > 1000) ms = 1000;      /* wake up now and then: the socket may be shut down */
        } else ms = 1000;
        struct pollfd p;
        p.fd = s; p.events = for_write ? POLLOUT : POLLIN; p.revents = 0;
        int r = ws_poll(&p, 1, ms);
        if (r > 0) return 1;               /* readable, writable, or an error to read */
        if (r < 0 && !WS_EINTR(ws_sockerr())) return -1;
    }
}

/* Reads up to n bytes. >0 bytes, 0 the peer closed, -1 error, -2 timed out. */
static int ws_read_some(WsConn *c, unsigned char *buf, int n, double deadline) {
    for (;;) {
        if (c->state == WS_CLOSED) return -1;
        if (c->ssl) {
            pthread_mutex_lock(&c->io);
            ERR_clear_error();
            int r = SSL_read(c->ssl, buf, n);
            int e = r > 0 ? 0 : SSL_get_error(c->ssl, r);
            pthread_mutex_unlock(&c->io);
            if (r > 0) return r;
            if (e == SSL_ERROR_ZERO_RETURN) return 0;
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                int w = ws_wait(c, e == SSL_ERROR_WANT_WRITE, deadline);
                if (w == 0) return -2;
                if (w < 0) return -1;
                continue;
            }
            if (e == SSL_ERROR_SYSCALL && ERR_peek_error() == 0) return 0;   /* peer closed without TLS close */
            return -1;
        }
        int r = (int)recv(c->sock, (char*)buf, n, 0);
        if (r > 0) return r;
        if (r == 0) return 0;
        int e = ws_sockerr();
        if (WS_EAGAIN(e) || WS_EINTR(e)) {
            int w = ws_wait(c, 0, deadline);
            if (w == 0) return -2;
            if (w < 0) return -1;
            continue;
        }
        return -1;
    }
}

/* Writes all n bytes; the caller holds wlock. 1 done, 0 timed out, -1 error. */
static int ws_write_all(WsConn *c, const unsigned char *buf, size_t n, double deadline) {
    size_t done = 0;
    while (done < n) {
        if (c->state == WS_CLOSED) return -1;
        int chunk = n - done > (1u << 30) ? (1 << 30) : (int)(n - done);
        if (c->ssl) {
            pthread_mutex_lock(&c->io);
            ERR_clear_error();
            int r = SSL_write(c->ssl, buf + done, chunk);
            int e = r > 0 ? 0 : SSL_get_error(c->ssl, r);
            pthread_mutex_unlock(&c->io);
            if (r > 0) { done += (size_t)r; continue; }
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                int w = ws_wait(c, e == SSL_ERROR_WANT_WRITE, deadline);
                if (w == 0) return 0;
                if (w < 0) return -1;
                continue;
            }
            return -1;
        }
#ifdef MSG_NOSIGNAL
        int r = (int)send(c->sock, (const char*)buf + done, chunk, MSG_NOSIGNAL);
#else
        int r = (int)send(c->sock, (const char*)buf + done, chunk, 0);
#endif
        if (r > 0) { done += (size_t)r; continue; }
        int e = ws_sockerr();
        if (r < 0 && (WS_EAGAIN(e) || WS_EINTR(e))) {
            int w = ws_wait(c, 1, deadline);
            if (w == 0) return 0;
            if (w < 0) return -1;
            continue;
        }
        return -1;
    }
    return 1;
}

/* ---- frames ---- */

/* Sends one frame. 1 sent, 0 timed out, -1 failed. */
static int ws_send_frame(WsConn *c, int opcode, const unsigned char *data, size_t len) {
    unsigned char head[14];
    size_t h = 0;
    head[h++] = (unsigned char)(0x80 | opcode);
    unsigned char maskbit = c->is_client ? 0x80 : 0;
    if (len < 126) head[h++] = (unsigned char)(maskbit | len);
    else if (len <= 0xFFFF) { head[h++] = maskbit | 126; head[h++] = (unsigned char)(len >> 8); head[h++] = (unsigned char)len; }
    else {
        head[h++] = maskbit | 127;
        for (int k = 7; k >= 0; k--) head[h++] = (unsigned char)((uint64_t)len >> (k * 8));
    }
    unsigned char key[4] = {0, 0, 0, 0};
    if (c->is_client) {
        if (RAND_bytes(key, 4) != 1) { uint32_t r = (uint32_t)rand() ^ (uint32_t)(uintptr_t)data; memcpy(key, &r, 4); }
        memcpy(head + h, key, 4); h += 4;
    }
    unsigned char *frame = malloc(h + len + 1);
    if (!frame) return -1;
    memcpy(frame, head, h);
    if (len) memcpy(frame + h, data, len);
    if (c->is_client) for (size_t i = 0; i < len; i++) frame[h + i] ^= key[i & 3];
    pthread_mutex_lock(&c->wlock);
    int r = ws_write_all(c, frame, h + len, ws_now() + c->send_timeout);
    pthread_mutex_unlock(&c->wlock);
    free(frame);
    return r;
}

/* Sends our close frame once. */
static void ws_send_close(WsConn *c, int code, const char *reason) {
    pthread_mutex_lock(&c->state_lock);
    int already = c->close_sent;
    c->close_sent = 1;
    pthread_mutex_unlock(&c->state_lock);
    if (already) return;
    unsigned char p[125];
    size_t n = 0;
    if (code) {
        p[0] = (unsigned char)(code >> 8); p[1] = (unsigned char)code; n = 2;
        size_t rl = reason ? strlen(reason) : 0;
        if (rl > 123) rl = 123;
        if (rl) { memcpy(p + 2, reason, rl); n += rl; }
    }
    ws_send_frame(c, 8, p, n);
}

/* UTF-8, a byte at a time (RFC 3629: no overlongs, no surrogates, nothing
   above U+10FFFF). 1 fine so far, 0 invalid. */
static int ws_utf8_step(WsConn *c, unsigned char b) {
    if (c->utf8_need == 0) {
        if (b < 0x80) return 1;
        if (b >= 0xC2 && b <= 0xDF) { c->utf8_need = 1; c->utf8_lo = 0x80; c->utf8_hi = 0xBF; return 1; }
        if (b == 0xE0) { c->utf8_need = 2; c->utf8_lo = 0xA0; c->utf8_hi = 0xBF; return 1; }
        if ((b >= 0xE1 && b <= 0xEC) || b == 0xEE || b == 0xEF) { c->utf8_need = 2; c->utf8_lo = 0x80; c->utf8_hi = 0xBF; return 1; }
        if (b == 0xED) { c->utf8_need = 2; c->utf8_lo = 0x80; c->utf8_hi = 0x9F; return 1; }
        if (b == 0xF0) { c->utf8_need = 3; c->utf8_lo = 0x90; c->utf8_hi = 0xBF; return 1; }
        if (b >= 0xF1 && b <= 0xF3) { c->utf8_need = 3; c->utf8_lo = 0x80; c->utf8_hi = 0xBF; return 1; }
        if (b == 0xF4) { c->utf8_need = 3; c->utf8_lo = 0x80; c->utf8_hi = 0x8F; return 1; }
        return 0;
    }
    if (b < c->utf8_lo || b > c->utf8_hi) return 0;
    c->utf8_need--; c->utf8_lo = 0x80; c->utf8_hi = 0xBF;
    return 1;
}

static int ws_utf8_whole(const unsigned char *p, size_t n) {
    WsConn tmp; memset(&tmp, 0, sizeof(tmp));
    for (size_t i = 0; i < n; i++) if (!ws_utf8_step(&tmp, p[i])) return 0;
    return tmp.utf8_need == 0;
}

/* Close codes a peer may send (RFC 6455 7.4 and the IANA registry):
   1004-1006 and 1015 are never sent on the wire. */
static int ws_close_code_ok(int code) {
    return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
}

/* Result of reading the next message. */
typedef struct {
    int kind;                 /* 1 text, 2 binary, 8 close, 0 timed out */
    unsigned char *data; size_t len;     /* malloc'd message (text/binary) */
    int code; char reason[128];          /* close */
} WsMsg;

/* Ends the connection after a protocol error: tells the peer why (unless
   the connection is already gone) and reports it as a close. */
static void ws_fail_conn(WsConn *c, int code, const char *why, WsMsg *out) {
    if (code != 1006) {
        ws_send_close(c, code, why);
        /* Stop sending but keep reading (and dropping) what the peer still
           sends, briefly: closing outright while data is arriving resets
           the connection, and the peer would never see our close frame. */
        if (!c->ssl) shutdown(c->sock,
#ifdef _WIN32
                              SD_SEND
#else
                              SHUT_WR
#endif
                              );
        unsigned char sink[16384];
        double until = ws_now() + 2;
        while (ws_read_some(c, sink, sizeof(sink), until) > 0) {}
    }
    pthread_mutex_lock(&c->state_lock);
    if (!c->close_code) { c->close_code = code; snprintf(c->close_reason, sizeof(c->close_reason), "%s", why); }
    pthread_mutex_unlock(&c->state_lock);
    ws_teardown(c);
    out->kind = 8; out->code = c->close_code;
    snprintf(out->reason, sizeof(out->reason), "%s", c->close_reason);
}

static int ws_msg_append(WsConn *c, const unsigned char *p, size_t n) {
    if (c->mlen + n + 1 > c->mcap) {
        size_t cap = c->mcap ? c->mcap : 4096;
        while (cap < c->mlen + n + 1) cap *= 2;
        unsigned char *m = realloc(c->msg, cap);
        if (!m) return 0;
        c->msg = m; c->mcap = cap;
    }
    memcpy(c->msg + c->mlen, p, n);
    c->mlen += n;
    return 1;
}

/* Reads frames until a whole message, a close, or the deadline (deadline < 0:
   no limit). The caller holds rlock. */
static void ws_next(WsConn *c, double deadline, WsMsg *out) {
    memset(out, 0, sizeof(*out));
    for (;;) {
        /* a whole frame in rbuf? */
        if (c->rlen >= 2) {
            unsigned char *b = c->rbuf;
            int fin = b[0] & 0x80, rsv = b[0] & 0x70, op = b[0] & 0x0F;
            int masked = b[1] & 0x80;
            uint64_t len = b[1] & 0x7F;
            size_t need = 2;
            if (len == 126) need += 2;
            else if (len == 127) need += 8;
            if (masked) need += 4;
            if (c->rlen >= need) {
                size_t h = 2;
                if (len == 126) { len = ((uint64_t)b[2] << 8) | b[3]; h = 4; }
                else if (len == 127) {
                    len = 0;
                    for (int k = 0; k < 8; k++) len = (len << 8) | b[2 + k];
                    h = 10;
                    if (len >> 63) { ws_fail_conn(c, 1002, "frame length too large", out); return; }
                }
                if (rsv) { ws_fail_conn(c, 1002, "reserved bits set", out); return; }
                if (op >= 8) {
                    if (!fin) { ws_fail_conn(c, 1002, "fragmented control frame", out); return; }
                    if (len > 125) { ws_fail_conn(c, 1002, "control frame too long", out); return; }
                    if (op > 10) { ws_fail_conn(c, 1002, "unknown opcode", out); return; }
                } else if (op > 2) { ws_fail_conn(c, 1002, "unknown opcode", out); return; }
                if (c->is_client && masked) { ws_fail_conn(c, 1002, "masked frame from the server", out); return; }
                if (!c->is_client && !masked) { ws_fail_conn(c, 1002, "unmasked frame from the client", out); return; }
                if (op < 8 && (uint64_t)c->mlen + len > c->max_message) { ws_fail_conn(c, 1009, "message too big", out); return; }
                if (op == 0 && c->msg_op == 0) { ws_fail_conn(c, 1002, "continuation without a message", out); return; }
                if ((op == 1 || op == 2) && c->msg_op != 0) { ws_fail_conn(c, 1002, "new message inside a fragmented one", out); return; }
                size_t total = h + (masked ? 4 : 0) + (size_t)len;
                if (c->rlen >= total) {
                    unsigned char *payload = b + h + (masked ? 4 : 0);
                    if (masked) {
                        unsigned char *key = b + h;
                        for (size_t i = 0; i < len; i++) payload[i] ^= key[i & 3];
                    }
                    int done_msg = 0;
                    if (op == 1 || op == 2 || op == 0) {
                        if (op != 0) { c->msg_op = op; c->mlen = 0; c->utf8_need = 0; }
                        if (c->msg_op == 1) {
                            for (size_t i = 0; i < len; i++)
                                if (!ws_utf8_step(c, payload[i])) { ws_fail_conn(c, 1007, "invalid UTF-8 in a text message", out); return; }
                            if (fin && c->utf8_need) { ws_fail_conn(c, 1007, "invalid UTF-8 in a text message", out); return; }
                        }
                        if (!ws_msg_append(c, payload, (size_t)len)) { ws_fail_conn(c, 1009, "out of memory", out); return; }
                        if (fin) done_msg = 1;
                    } else if (op == 8) {
                        int code = 1005;
                        char reason[128] = "";
                        if (len == 1) { ws_fail_conn(c, 1002, "close frame with a one-byte body", out); return; }
                        if (len >= 2) {
                            code = (payload[0] << 8) | payload[1];
                            if (!ws_close_code_ok(code)) { ws_fail_conn(c, 1002, "invalid close code", out); return; }
                            if (!ws_utf8_whole(payload + 2, (size_t)len - 2)) { ws_fail_conn(c, 1007, "invalid UTF-8 in the close reason", out); return; }
                            size_t rl = (size_t)len - 2 < sizeof(reason) - 1 ? (size_t)len - 2 : sizeof(reason) - 1;
                            memcpy(reason, payload + 2, rl); reason[rl] = '\0';
                        }
                        pthread_mutex_lock(&c->state_lock);
                        c->close_received = 1;
                        if (!c->close_code) { c->close_code = code; snprintf(c->close_reason, sizeof(c->close_reason), "%s", reason); }
                        pthread_mutex_unlock(&c->state_lock);
                        /* answer with the same code (none when it sent none) */
                        ws_send_close(c, code == 1005 ? 0 : code, reason);
                        ws_teardown(c);
                        out->kind = 8; out->code = c->close_code;
                        snprintf(out->reason, sizeof(out->reason), "%s", c->close_reason);
                        return;
                    } else if (op == 9) {
                        if (!c->close_sent) ws_send_frame(c, 10, payload, (size_t)len);
                    }   /* op == 10: a pong; nothing to do */
                    memmove(c->rbuf, c->rbuf + total, c->rlen - total);
                    c->rlen -= total;
                    if (done_msg) {
                        out->kind = c->msg_op;
                        out->data = c->msg; out->len = c->mlen;
                        c->msg = NULL; c->mlen = c->mcap = 0; c->msg_op = 0;
                        return;
                    }
                    continue;
                }
                /* make room for the whole frame */
                if (total > c->rcap) {
                    unsigned char *nb = realloc(c->rbuf, total);
                    if (!nb) { ws_fail_conn(c, 1009, "out of memory", out); return; }
                    c->rbuf = nb; c->rcap = total;
                }
            }
        }
        if (c->rcap - c->rlen < 65536) {
            size_t cap = c->rcap ? c->rcap * 2 : 65536;
            while (cap - c->rlen < 65536) cap *= 2;
            unsigned char *nb = realloc(c->rbuf, cap);
            if (!nb) { ws_fail_conn(c, 1009, "out of memory", out); return; }
            c->rbuf = nb; c->rcap = cap;
        }
        size_t room = c->rcap - c->rlen;
        int r = ws_read_some(c, c->rbuf + c->rlen, room > (1u << 30) ? (1 << 30) : (int)room, deadline);
        if (r > 0) { c->rlen += (size_t)r; continue; }
        if (r == -2) { out->kind = 0; return; }
        /* the connection ended without a close frame */
        pthread_mutex_lock(&c->state_lock);
        int had = c->close_code;
        pthread_mutex_unlock(&c->state_lock);
        if (had) {
            ws_teardown(c);
            out->kind = 8; out->code = c->close_code;
            snprintf(out->reason, sizeof(out->reason), "%s", c->close_reason);
            return;
        }
        ws_fail_conn(c, 1006, "the connection was lost", out);
        return;
    }
}

/* ---- handshake helpers ---- */

static char *ws_b64(const unsigned char *in, size_t n) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *out = malloc((n + 2) / 3 * 4 + 1), *o = out;
    if (!out) return NULL;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        *o++ = t[(v >> 18) & 63]; *o++ = t[(v >> 12) & 63];
        *o++ = i + 1 < n ? t[(v >> 6) & 63] : '=';
        *o++ = i + 2 < n ? t[v & 63] : '=';
    }
    *o = '\0';
    return out;
}

/* base64(SHA-1(key + GUID)) into out (29 bytes). */
static void ws_accept_key(const char *key, char *out) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s%s", key, WS_GUID);
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)buf, strlen(buf), md);
    char *b = ws_b64(md, SHA_DIGEST_LENGTH);
    snprintf(out, 32, "%s", b ? b : "");
    free(b);
}

/* The value of header `name` in the header block (case-insensitive), or 0. */
static int ws_header(const char *head, const char *name, char *out, size_t outsz) {
    size_t nl = strlen(name);
    const char *p = strstr(head, "\r\n");
    while (p && p[2] != '\r' && p[2] != '\0') {
        const char *line = p + 2;
        const char *eol = strstr(line, "\r\n");
        if (!eol) eol = line + strlen(line);
        if ((size_t)(eol - line) > nl && strncasecmp(line, name, nl) == 0 && line[nl] == ':') {
            const char *v = line + nl + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t vl = (size_t)(eol - v);
            while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) vl--;
            if (vl >= outsz) vl = outsz - 1;
            memcpy(out, v, vl); out[vl] = '\0';
            return 1;
        }
        p = *eol ? eol : NULL;
    }
    return 0;
}

/* Whether a comma-separated header value has this token (any case). */
static int ws_has_token(const char *value, const char *token) {
    size_t tl = strlen(token);
    const char *p = value;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *e = p;
        while (*e && *e != ',') e++;
        const char *t = e;
        while (t > p && (t[-1] == ' ' || t[-1] == '\t')) t--;
        if ((size_t)(t - p) == tl && strncasecmp(p, token, tl) == 0) return 1;
        p = e;
    }
    return 0;
}

static int ws_option_double(Value options, const char *name, double *out) {
    if (GET_TYPE(options) != VAL_DICT) return 0;
    Value v = dict_get(AS_DICT(options), name);
    if (GET_TYPE(v) == VAL_INT) { *out = (double)AS_INT(v); return 1; }
    if (GET_TYPE(v) == VAL_FLOAT) { *out = AS_FLOAT(v); return 1; }
    return 0;
}

static void ws_apply_options(WsConn *c, Value options) {
    double d;
    if (ws_option_double(options, "max_message", &d) && d >= 1) c->max_message = d > 2147483647.0 ? 2147483647u : (size_t)d;
    if (ws_option_double(options, "send_timeout", &d) && d > 0) c->send_timeout = d;
}

/* Appends formatted text to a growing buffer. Out of memory, the buffer is
   freed and set to NULL, and later calls do nothing. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static void ws_addf(char **buf, size_t *cap, size_t *n, const char *fmt, ...) {
    if (!*buf) return;
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int w = vsnprintf(*buf + *n, *cap - *n, fmt, ap);
        va_end(ap);
        if (w < 0) return;
        if ((size_t)w < *cap - *n) { *n += (size_t)w; return; }
        size_t ncap = (*cap + (size_t)w + 1) * 2;
        char *nb = realloc(*buf, ncap);
        if (!nb) { free(*buf); *buf = NULL; return; }
        *buf = nb; *cap = ncap;
    }
}

/* ---- natives ---- */

/* ws_native.connect(url, options): options "headers" (dict), "protocols"
   (list of text), "timeout" (connect and handshake, seconds, default 30),
   "verify" (check the TLS certificate, default true), "max_message",
   "send_timeout". */
static Value ws_connect_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_STRING) return ws_fail("websocket.connect: give a ws:// or wss:// URL%s", "");
    const char *url = AS_STRING(args[0]);
    Value options = argc > 1 ? args[1] : value_none();
    int tls;
    const char *p;
    if (strncasecmp(url, "wss://", 6) == 0) { tls = 1; p = url + 6; }
    else if (strncasecmp(url, "ws://", 5) == 0) { tls = 0; p = url + 5; }
    else return ws_fail("websocket.connect: the URL must start with ws:// or wss:// (got %s)", url);
    char host[256]; int port = tls ? 443 : 80;
    const char *hs = p, *he;
    int v6 = 0;
    if (*p == '[') { hs = p + 1; he = strchr(hs, ']'); if (!he) return ws_fail("websocket.connect: bad IPv6 address in %s", url); v6 = 1; p = he + 1; }
    else { he = p; while (*he && *he != ':' && *he != '/' && *he != '?' && *he != '#') he++; p = he; }
    if (he == hs || (size_t)(he - hs) >= sizeof(host)) return ws_fail("websocket.connect: no host in %s", url);
    memcpy(host, hs, (size_t)(he - hs)); host[he - hs] = '\0';
    if (*p == ':') {
        char *end;
        long pt = strtol(p + 1, &end, 10);
        if (end == p + 1 || pt < 1 || pt > 65535) return ws_fail("websocket.connect: bad port in %s", url);
        port = (int)pt; p = end;
    }
    const char *path = (*p == '/' || *p == '?') ? p : "/";
    char pathbuf[2048];
    if (*p == '?') { snprintf(pathbuf, sizeof(pathbuf), "/%s", p); path = pathbuf; }
    if (strchr(path, '#')) { snprintf(pathbuf, sizeof(pathbuf), "%.*s", (int)(strchr(path, '#') - path), path); path = pathbuf; }
    for (const char *q = path; *q; q++) if ((unsigned char)*q <= ' ') return ws_fail("websocket.connect: the URL has spaces or control characters: %s", url);

    double timeout = 30; ws_option_double(options, "timeout", &timeout);
    if (timeout <= 0) timeout = 30;
    int verify = 1;
    if (GET_TYPE(options) == VAL_DICT) { Value v = dict_get(AS_DICT(options), "verify"); if (GET_TYPE(v) == VAL_BOOL) verify = AS_BOOL(v); }

    /* the request, built while we hold the GIL */
    unsigned char keyraw[16];
    if (RAND_bytes(keyraw, 16) != 1) return ws_fail("websocket.connect: the secure random source failed%s", "");
    char *key = ws_b64(keyraw, 16);
    size_t cap = 4096, n = 0;
    char *req = malloc(cap);
    if (!req || !key) { free(req); free(key); return ws_fail("websocket.connect: out of memory%s", ""); }
    int default_port = port == (tls ? 443 : 80);
    char hostport[300];
    if (default_port) snprintf(hostport, sizeof(hostport), "%s%s%s", v6 ? "[" : "", host, v6 ? "]" : "");
    else snprintf(hostport, sizeof(hostport), "%s%s%s:%d", v6 ? "[" : "", host, v6 ? "]" : "", port);
#define WS_ADD(...) ws_addf(&req, &cap, &n, __VA_ARGS__)
    WS_ADD("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n", path, hostport, key);
    char offered[1024] = "";
    int has_ua = 0;
    if (GET_TYPE(options) == VAL_DICT) {
        Value pv = dict_get(AS_DICT(options), "protocols");
        if (GET_TYPE(pv) == VAL_LIST) {
            List *l = AS_LIST(pv);
            for (int i = 0; i < l->size; i++) {
                if (GET_TYPE(l->items[i]) != VAL_STRING) continue;
                const char *s = AS_STRING(l->items[i]);
                if (!*s || strpbrk(s, " ,\r\n\t")) { free(req); free(key); return ws_fail("websocket.connect: a subprotocol name cannot be empty or contain spaces or commas: %s", s); }
                if (strlen(offered) + strlen(s) + 3 < sizeof(offered)) { if (*offered) strcat(offered, ", "); strcat(offered, s); }
            }
            if (*offered) WS_ADD("Sec-WebSocket-Protocol: %s\r\n", offered);
        }
        Value hv = dict_get(AS_DICT(options), "headers");
        if (GET_TYPE(hv) == VAL_DICT) {
            Dict *h = AS_DICT(hv);
            for (int i = 0; i < h->capacity; i++) {
                if (GET_TYPE(h->keys[i]) != VAL_STRING) continue;
                const char *k = (const char*)(uintptr_t)(h->keys[i] & MASK_PAYLOAD);
                char *val = value_to_new_cstring(h->values[i]);
                if (strpbrk(k, "\r\n:") || (val && strpbrk(val, "\r\n"))) {
                    free(val); free(req); free(key);
                    return ws_fail("websocket.connect: a header name or value contains a line break%s", "");
                }
                if (strcasecmp(k, "User-Agent") == 0) has_ua = 1;
                WS_ADD("%s: %s\r\n", k, val ? val : "");
                free(val);
            }
        }
    }
    if (!has_ua) WS_ADD("User-Agent: GlowLang\r\n");
    WS_ADD("\r\n");
#undef WS_ADD
    if (!req) { free(key); return ws_fail("websocket.connect: out of memory%s", ""); }

    char err[512] = "";
    char status_line[256] = "";
    char protocol[128] = "";
    char *leftover = NULL; size_t leftover_n = 0;
    SSL *ssl = NULL;
    ws_sock_t s = WS_BAD_SOCK;
    int ok = 0;
    SSL_CTX *ctx = tls ? (SSL_CTX*)glow_tls_client_ctx() : NULL;   /* made once, under the GIL */
    glow_gil_release();
    do {
        intptr_t si = glow_net_connect(host, port, timeout, err, sizeof(err));
        if (si < 0) break;
        s = (ws_sock_t)si;
        double deadline = ws_now() + timeout;
        if (tls) {
            if (!ctx || !(ssl = SSL_new(ctx))) { snprintf(err, sizeof(err), "cannot start TLS"); break; }
            SSL_set_fd(ssl, (int)s);
            if (!v6) SSL_set_tlsext_host_name(ssl, host);
            if (verify) {
                SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);
                if (v6) X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), host);
                else SSL_set1_host(ssl, host);
            }
            if (SSL_connect(ssl) <= 0) {
                long vr = SSL_get_verify_result(ssl);
                if (verify && vr != X509_V_OK) snprintf(err, sizeof(err), "TLS certificate of %s not trusted: %s", host, X509_verify_cert_error_string(vr));
                else snprintf(err, sizeof(err), "TLS handshake with %s failed", host);
                break;
            }
        }
        /* send the request (blocking socket, timeouts set by connect) */
        size_t sent = 0;
        while (sent < n) {
            int w = ssl ? SSL_write(ssl, req + sent, (int)(n - sent)) : (int)send(s, req + sent, (int)(n - sent), 0);
            if (w <= 0) break;
            sent += (size_t)w;
        }
        if (sent < n) { snprintf(err, sizeof(err), "sending the handshake to %s failed", host); break; }
        /* read the answer's head */
        char head[WS_MAX_HEAD + 1];
        size_t got = 0;
        char *end = NULL;
        while (!end && got < WS_MAX_HEAD) {
            if (ws_now() > deadline) break;
            int r = ssl ? SSL_read(ssl, head + got, (int)(WS_MAX_HEAD - got)) : (int)recv(s, head + got, (int)(WS_MAX_HEAD - got), 0);
            if (r <= 0) break;
            got += (size_t)r; head[got] = '\0';
            end = strstr(head, "\r\n\r\n");
        }
        if (!end) { snprintf(err, sizeof(err), got ? "%s sent no valid handshake answer" : "%s did not answer the handshake", host); break; }
        size_t hlen = (size_t)(end - head) + 4;
        end[2] = '\0';                      /* header block ends with one CRLF */
        const char *eol = strstr(head, "\r\n");
        snprintf(status_line, sizeof(status_line), "%.*s", (int)(eol - head), head);
        int status = 0;
        if (strncmp(head, "HTTP/1.1 ", 9) == 0 || strncmp(head, "HTTP/1.0 ", 9) == 0) status = atoi(head + 9);
        if (status != 101) {
            snprintf(err, sizeof(err), "the server answered \"%s\" instead of switching to WebSocket", status_line[0] ? status_line : "nothing");
            break;
        }
        char v[256], want[32];
        if (!ws_header(head, "Upgrade", v, sizeof(v)) || !ws_has_token(v, "websocket")) { snprintf(err, sizeof(err), "the server's answer has no \"Upgrade: websocket\""); break; }
        if (!ws_header(head, "Connection", v, sizeof(v)) || !ws_has_token(v, "upgrade")) { snprintf(err, sizeof(err), "the server's answer has no \"Connection: Upgrade\""); break; }
        ws_accept_key(key, want);
        if (!ws_header(head, "Sec-WebSocket-Accept", v, sizeof(v)) || strcmp(v, want) != 0) { snprintf(err, sizeof(err), "the server's Sec-WebSocket-Accept is wrong"); break; }
        if (ws_header(head, "Sec-WebSocket-Extensions", v, sizeof(v)) && *v) { snprintf(err, sizeof(err), "the server chose extensions (%s) that were not offered", v); break; }
        if (ws_header(head, "Sec-WebSocket-Protocol", protocol, sizeof(protocol)) && *protocol) {
            if (!*offered || !ws_has_token(offered, protocol) || strchr(protocol, ',')) { snprintf(err, sizeof(err), "the server chose the subprotocol \"%s\", which was not offered", protocol); break; }
        }
        if (got > hlen) {
            leftover_n = got - hlen;
            leftover = malloc(leftover_n);
            if (leftover) memcpy(leftover, head + hlen, leftover_n);
        }
        ok = 1;
    } while (0);
    if (!ok) {
        if (ssl) SSL_free(ssl);
        if (s != WS_BAD_SOCK) ws_closesock(s);
    }
    glow_gil_acquire();
    free(req); free(key);
    if (!ok) { free(leftover); return ws_fail("websocket.connect: %s", err); }
    ws_nonblocking(s);
    WsConn *c = ws_conn_new(s, ssl, 1);
    if (!c) { free(leftover); if (ssl) SSL_free(ssl); ws_closesock(s); return ws_fail("websocket.connect: out of memory%s", ""); }
    if (leftover) { c->rbuf = (unsigned char*)leftover; c->rlen = c->rcap = leftover_n; }
    snprintf(c->protocol, sizeof(c->protocol), "%s", protocol);
    ws_apply_options(c, options);
    int64_t id = ws_register(c);
    if (id < 0) { ws_conn_free(c); return ws_fail("websocket.connect: too many open WebSocket connections%s", ""); }
    return value_int(id);
}

/* Sends a whole HTTP answer on a socket we are about to give up on. */
static void ws_send_plain(ws_sock_t s, const char *text) {
    size_t n = strlen(text), done = 0;
    while (done < n) {
        int w = (int)send(s, text + done, (int)(n - done), 0);
        if (w <= 0) break;
        done += (size_t)w;
    }
}

/* ws_native.accept(socket, request, options): takes over an accepted
   connection (http.accept) and answers its upgrade request. request is the
   raw request http.read returned, or none to read it here. options:
   "protocols" (the subprotocols this server speaks, preferred first),
   "max_message", "send_timeout". On a bad request it answers 400 (or 426
   for another WebSocket version) and raises; the socket stays the caller's
   to close. */
static Value ws_accept_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return ws_fail("websocket.accept: give the connection from http.accept%s", "");
    ws_sock_t s = (ws_sock_t)AS_INT(args[0]);
    Value options = argc > 2 ? args[2] : value_none();
    char *raw = NULL; size_t rawn = 0;
    if (argc > 1 && GET_TYPE(args[1]) == VAL_STRING) {
        rawn = rope_length(args[1]);
        raw = malloc(rawn + 1);
        if (!raw) return ws_fail("websocket.accept: out of memory%s", "");
        memcpy(raw, AS_STRING(args[1]), rawn); raw[rawn] = '\0';
    } else {
        /* read the request head ourselves */
        raw = malloc(WS_MAX_HEAD + 1);
        if (!raw) return ws_fail("websocket.accept: out of memory%s", "");
        double deadline = ws_now() + 30;
        glow_gil_release();
        while (rawn < WS_MAX_HEAD) {
            if (ws_now() > deadline) break;
            int r = (int)recv(s, raw + rawn, (int)(WS_MAX_HEAD - rawn), 0);
            if (r <= 0) break;
            rawn += (size_t)r; raw[rawn] = '\0';
            if (strstr(raw, "\r\n\r\n")) break;
        }
        glow_gil_acquire();
        raw[rawn] = '\0';
    }
    char *end = strstr(raw, "\r\n\r\n");
    const char *why = NULL;
    int status = 400;
    char key[64], v[512];
    char chosen[128] = "";
    if (!end) why = "the request is incomplete";
    else {
        end[2] = '\0';
        if (strncmp(raw, "GET ", 4) != 0) why = "the request must be a GET";
        else if (!strstr(raw, " HTTP/1.1\r\n")) why = "the request must be HTTP/1.1";
        else if (!ws_header(raw, "Upgrade", v, sizeof(v)) || !ws_has_token(v, "websocket")) why = "the request has no \"Upgrade: websocket\"";
        else if (!ws_header(raw, "Connection", v, sizeof(v)) || !ws_has_token(v, "upgrade")) why = "the request has no \"Connection: Upgrade\"";
        else if (!ws_header(raw, "Sec-WebSocket-Version", v, sizeof(v)) || strcmp(v, "13") != 0) { why = "only WebSocket version 13 is supported"; status = 426; }
        else if (!ws_header(raw, "Sec-WebSocket-Key", key, sizeof(key))) why = "the request has no Sec-WebSocket-Key";
        else {
            /* the key is 16 bytes in base64: 24 characters ending in == */
            size_t kl = strlen(key);
            int good = kl == 24 && key[22] == '=' && key[23] == '=';
            for (size_t i = 0; good && i < 22; i++) good = isalnum((unsigned char)key[i]) || key[i] == '+' || key[i] == '/';
            if (!good) why = "the Sec-WebSocket-Key is not 16 bytes in base64";
        }
    }
    if (!why && GET_TYPE(options) == VAL_DICT && ws_header(raw, "Sec-WebSocket-Protocol", v, sizeof(v))) {
        Value pv = dict_get(AS_DICT(options), "protocols");
        if (GET_TYPE(pv) == VAL_LIST) {
            List *l = AS_LIST(pv);
            for (int i = 0; i < l->size && !*chosen; i++)
                if (GET_TYPE(l->items[i]) == VAL_STRING && ws_has_token(v, AS_STRING(l->items[i])))
                    snprintf(chosen, sizeof(chosen), "%s", AS_STRING(l->items[i]));
        }
    }
    if (why) {
        char resp[512];
        if (status == 426) snprintf(resp, sizeof(resp), "HTTP/1.1 426 Upgrade Required\r\nSec-WebSocket-Version: 13\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(why), why);
        else snprintf(resp, sizeof(resp), "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(why), why);
        glow_gil_release();
        ws_send_plain(s, resp);
        glow_gil_acquire();
        free(raw);
        return ws_fail("websocket.accept: %s", why);
    }
    char accept_key[32];
    ws_accept_key(key, accept_key);
    char resp[512];
    if (*chosen) snprintf(resp, sizeof(resp), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: %s\r\n\r\n", accept_key, chosen);
    else snprintf(resp, sizeof(resp), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
    size_t head_len = (size_t)(end - raw) + 4;
    glow_gil_release();
    ws_send_plain(s, resp);
    glow_gil_acquire();
    ws_nonblocking(s);
    WsConn *c = ws_conn_new(s, NULL, 0);
    if (!c) { free(raw); return ws_fail("websocket.accept: out of memory%s", ""); }
    if (rawn > head_len) {
        size_t extra = rawn - head_len;
        c->rbuf = malloc(extra);
        if (c->rbuf) { memcpy(c->rbuf, raw + head_len, extra); c->rlen = c->rcap = extra; }
    }
    free(raw);
    snprintf(c->protocol, sizeof(c->protocol), "%s", chosen);
    ws_apply_options(c, options);
    int64_t id = ws_register(c);
    if (id < 0) { c->sock = WS_BAD_SOCK; ws_conn_free(c); return ws_fail("websocket.accept: too many open WebSocket connections%s", ""); }
    return value_int(id);
}

static WsConn *ws_arg(int argc, Value *args, const char *fn) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) { ws_fail("%s: not a WebSocket connection", fn); return NULL; }
    WsConn *c = ws_get(AS_INT(args[0]));
    if (!c) ws_fail("%s: the connection is closed", fn);
    return c;
}

/* ws_native.send(id, data, binary): a text message, or binary when binary
   is true. Text must be valid UTF-8. */
static Value ws_send_native(int argc, Value *args) {
    WsConn *c = ws_arg(argc, args, "websocket.send");
    if (!c) return value_none();
    if (argc < 2 || GET_TYPE(args[1]) != VAL_STRING) { ws_release(c); return ws_fail("websocket.send: the message must be text%s", ""); }
    int binary = argc > 2 && GET_TYPE(args[2]) == VAL_BOOL && AS_BOOL(args[2]);
    size_t n = rope_length(args[1]);
    const unsigned char *data = (const unsigned char*)AS_STRING(args[1]);
    if (!binary && !ws_utf8_whole(data, n)) { ws_release(c); return ws_fail("websocket.send: the text is not valid UTF-8 (send bytes with binary = true)%s", ""); }
    if (c->state == WS_CLOSED || c->close_sent) { ws_release(c); return ws_fail("websocket.send: the connection is closed%s", ""); }
    if (c->write_broken) { ws_release(c); return ws_fail("websocket.send: the connection was lost%s", ""); }
    unsigned char *copy = malloc(n ? n : 1);
    if (!copy) { ws_release(c); return ws_fail("websocket.send: out of memory%s", ""); }
    memcpy(copy, data, n);
    glow_gil_release();
    int r = ws_send_frame(c, binary ? 2 : 1, copy, n);
    glow_gil_acquire();
    free(copy);
    if (r <= 0) {
        /* Part of a frame may have gone out, so nothing more can be sent.
           The connection stays readable: receive() still gets the peer's
           close frame (it often explains the failure) or 1006. */
        c->write_broken = 1;
        ws_release(c);
        return ws_fail(r == 0 ? "websocket.send: timed out (the other side is not reading)%s" : "websocket.send: the connection was lost%s", "");
    }
    ws_release(c);
    return value_none();
}

static Value ws_msg_value(WsMsg *m) {
    Dict *d = dict_new(4);
    if (m->kind == 8) {
        dict_set(d, "type", value_string_copy("close"));
        dict_set(d, "code", value_int(m->code));
        dict_set(d, "reason", value_string_copy(m->reason));
    } else {
        dict_set(d, "type", value_string_copy(m->kind == 1 ? "text" : "binary"));
        dict_set(d, "data", value_string_copy_len((const char*)(m->data ? m->data : (unsigned char*)""), m->len));
    }
    return value_dict(d);
}

/* ws_native.recv(id, timeout): the next message, waiting at most timeout
   seconds (none: as long as it takes); none when it timed out. After the
   connection closes it returns the close: {"type": "close", "code", "reason"}. */
static Value ws_recv_native(int argc, Value *args) {
    if (argc >= 1 && GET_TYPE(args[0]) == VAL_INT) {
        WsConn *c = ws_get(AS_INT(args[0]));
        if (!c) return ws_fail("websocket.receive: the connection is closed%s", "");
        double deadline = -1;
        if (argc > 1 && (GET_TYPE(args[1]) == VAL_INT || GET_TYPE(args[1]) == VAL_FLOAT)) {
            double t = GET_TYPE(args[1]) == VAL_INT ? (double)AS_INT(args[1]) : AS_FLOAT(args[1]);
            deadline = ws_now() + (t > 0 ? t : 0);
        }
        WsMsg m;
        glow_gil_release();
        pthread_mutex_lock(&c->rlock);
        if (c->state == WS_CLOSED) {
            memset(&m, 0, sizeof(m));
            m.kind = 8; m.code = c->close_code ? c->close_code : 1006;
            snprintf(m.reason, sizeof(m.reason), "%s", c->close_reason);
        } else ws_next(c, deadline, &m);
        pthread_mutex_unlock(&c->rlock);
        glow_gil_acquire();
        Value out = m.kind == 0 ? value_none() : ws_msg_value(&m);
        free(m.data);
        ws_release(c);
        return out;
    }
    return ws_fail("websocket.receive: not a WebSocket connection%s", "");
}

/* ws_native.ping(id, data): a ping (at most 125 bytes); the other side
   answers with a pong, which receive() takes care of. */
static Value ws_ping_native(int argc, Value *args) {
    WsConn *c = ws_arg(argc, args, "websocket.ping");
    if (!c) return value_none();
    const char *data = argc > 1 && GET_TYPE(args[1]) == VAL_STRING ? AS_STRING(args[1]) : "";
    size_t n = argc > 1 && GET_TYPE(args[1]) == VAL_STRING ? rope_length(args[1]) : 0;
    if (n > 125) { ws_release(c); return ws_fail("websocket.ping: at most 125 bytes%s", ""); }
    if (c->state == WS_CLOSED || c->close_sent) { ws_release(c); return ws_fail("websocket.ping: the connection is closed%s", ""); }
    unsigned char buf[125];
    memcpy(buf, data, n);
    glow_gil_release();
    int r = ws_send_frame(c, 9, buf, n);
    glow_gil_acquire();
    ws_release(c);
    if (r <= 0) return ws_fail("websocket.ping: the connection was lost%s", "");
    return value_none();
}

/* ws_native.close(id, code, reason): starts the closing handshake and waits
   (up to 5 seconds) for the other side's answer, unless another thread is
   receiving, in which case that thread sees it. Closing twice is fine. */
static Value ws_close_native(int argc, Value *args) {
    if (argc < 1 || GET_TYPE(args[0]) != VAL_INT) return ws_fail("websocket.close: not a WebSocket connection%s", "");
    WsConn *c = ws_get(AS_INT(args[0]));
    if (!c) return value_none();               /* already closed */
    int code = 1000;
    if (argc > 1 && GET_TYPE(args[1]) == VAL_INT) code = (int)AS_INT(args[1]);
    if (!ws_close_code_ok(code)) {
        ws_release(c);
        return ws_fail("websocket.close: the code must be 1000-1003, 1007-1014 or 3000-4999%s", "");
    }
    char reason[124] = "";
    if (argc > 2 && GET_TYPE(args[2]) == VAL_STRING) {
        size_t rl = rope_length(args[2]);
        if (rl > 123) { ws_release(c); return ws_fail("websocket.close: the reason must be at most 123 bytes%s", ""); }
        memcpy(reason, AS_STRING(args[2]), rl); reason[rl] = '\0';
    }
    glow_gil_release();
    pthread_mutex_lock(&c->state_lock);
    if (!c->close_code) { c->close_code = code; snprintf(c->close_reason, sizeof(c->close_reason), "%s", reason); }
    pthread_mutex_unlock(&c->state_lock);
    c->close_deadline = ws_now() + 5;   /* a receiving thread gives up then too */
    ws_send_close(c, code, reason);
    if (ws_mutex_trylock(&c->rlock) == 0) {
        double deadline = ws_now() + 5;
        while (c->state != WS_CLOSED) {
            WsMsg m;
            ws_next(c, deadline, &m);
            free(m.data);
            if (m.kind == 0) break;             /* no answer in time */
        }
        ws_teardown(c);
        pthread_mutex_unlock(&c->rlock);
    }
    glow_gil_acquire();
    ws_release(c);
    return value_none();
}

/* ws_native.info(id): {"open", "protocol", "close_code", "close_reason"}. */
static Value ws_info_native(int argc, Value *args) {
    Dict *d = dict_new(4);
    WsConn *c = argc >= 1 && GET_TYPE(args[0]) == VAL_INT ? ws_get(AS_INT(args[0])) : NULL;
    if (!c) {
        dict_set(d, "open", value_bool(0));
        return value_dict(d);
    }
    pthread_mutex_lock(&c->state_lock);
    int open = c->state != WS_CLOSED && !c->close_sent;
    int code = c->close_code;
    char reason[128]; snprintf(reason, sizeof(reason), "%s", c->close_reason);
    pthread_mutex_unlock(&c->state_lock);
    dict_set(d, "open", value_bool(open));
    dict_set(d, "protocol", value_string_copy(c->protocol));
    if (code) { dict_set(d, "close_code", value_int(code)); dict_set(d, "close_reason", value_string_copy(reason)); }
    ws_release(c);
    return value_dict(d);
}

void glow_websocket_init(void) {
    Dict *ws = dict_new(8);
    dict_set(ws, "connect", native_func_new("connect", ws_connect_native));
    dict_set(ws, "accept", native_func_new("accept", ws_accept_native));
    dict_set(ws, "send", native_func_new("send", ws_send_native));
    dict_set(ws, "recv", native_func_new("recv", ws_recv_native));
    dict_set(ws, "ping", native_func_new("ping", ws_ping_native));
    dict_set(ws, "close", native_func_new("close", ws_close_native));
    dict_set(ws, "info", native_func_new("info", ws_info_native));
    var_set("ws_native", value_dict(ws));
}
