"""Raw-socket WebSocket client that sends malformed frames to the GlowLang
server and checks the close code it answers with (RFC 6455)."""
import base64, hashlib, os, socket, struct, sys

PORT = int(sys.argv[1])
fails = 0

def handshake(extra=b""):
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    key = base64.b64encode(os.urandom(16))
    s.sendall(b"GET /x HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Key: " + key + b"\r\nSec-WebSocket-Version: 13\r\n" + extra + b"\r\n")
    head = b""
    while b"\r\n\r\n" not in head:
        chunk = s.recv(1)
        if not chunk:
            break
        head += chunk
    want = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    return s, head, want

def frame(op, payload, fin=True, mask=True, rsv=0, length=None):
    b0 = (0x80 if fin else 0) | (rsv << 4) | op
    n = len(payload) if length is None else length
    mbit = 0x80 if mask else 0
    if n < 126:
        h = bytes([b0, mbit | n])
    elif n < 65536:
        h = bytes([b0, mbit | 126]) + struct.pack(">H", n)
    else:
        h = bytes([b0, mbit | 127]) + struct.pack(">Q", n)
    if mask:
        k = os.urandom(4)
        return h + k + bytes(c ^ k[i % 4] for i, c in enumerate(payload))
    return h + payload

def read_frame(s):
    def need(n):
        b = b""
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c:
                raise EOFError
            b += c
        return b
    b0, b1 = need(2)
    n = b1 & 0x7F
    if n == 126:
        n = struct.unpack(">H", need(2))[0]
    elif n == 127:
        n = struct.unpack(">Q", need(8))[0]
    if b1 & 0x80:
        raise AssertionError("server masked a frame")
    return b0 & 0x0F, need(n), bool(b0 & 0x80)

def expect_close(name, frames, code):
    global fails
    s, head, _ = handshake()
    for f in frames:
        s.sendall(f)
    try:
        while True:
            op, payload, _ = read_frame(s)
            if op == 8:
                got = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else None
                ok = got == code
                print(("ok:   " if ok else "FAIL: ") + name, "->", got, payload[2:].decode(errors="replace"))
                fails += not ok
                break
    except (EOFError, socket.timeout, ConnectionResetError) as e:
        print("FAIL: " + name, "-> no close frame", type(e).__name__)
        fails += 1
    s.close()

def expect_echo(name, frames, want_op, want_payload):
    global fails
    s, head, _ = handshake()
    for f in frames:
        s.sendall(f)
    op, payload, fin = read_frame(s)
    while op == 10 and want_op != 10:   # pongs
        op, payload, fin = read_frame(s)
    ok = op == want_op and payload == want_payload
    print(("ok:   " if ok else "FAIL: ") + name, op, payload[:40])
    fails += not ok
    s.sendall(frame(8, struct.pack(">H", 1000)))
    s.close()

# handshake
s, head, want = handshake()
ok = head.startswith(b"HTTP/1.1 101") and b"Sec-WebSocket-Accept: " + want in head
print(("ok:   " if ok else "FAIL: ") + "handshake accept key"); fails += not ok
s.close()

# fragmentation with a ping in between; the echo server sends back the whole message
expect_echo("fragmented text with interleaved ping",
            [frame(1, b"Hel", fin=False), frame(9, b"ping!"), frame(0, b"lo ", fin=False), frame(0, b"world")], 1, b"echo: Hello world")
expect_echo("text split inside a UTF-8 character", [frame(1, "न".encode()[:2], fin=False), frame(0, "न".encode()[2:])], 1, "echo: न".encode())
expect_echo("empty text", [frame(1, b"")], 1, b"echo: ")
expect_echo("ping is answered with the same payload", [frame(9, b"abc")], 10, b"abc")

expect_close("unmasked frame from a client", [frame(1, b"hi", mask=False)], 1002)
expect_close("reserved bit set", [frame(1, b"hi", rsv=1)], 1002)
expect_close("unknown opcode 3", [frame(3, b"")], 1002)
expect_close("unknown control opcode 11", [frame(11, b"")], 1002)
expect_close("fragmented ping", [frame(9, b"x", fin=False)], 1002)
expect_close("control frame over 125 bytes", [frame(9, b"x" * 126)], 1002)
expect_close("continuation with nothing to continue", [frame(0, b"x")], 1002)
expect_close("new text inside a fragmented one", [frame(1, b"a", fin=False), frame(1, b"b")], 1002)
expect_close("invalid UTF-8", [frame(1, b"\xc3\x28")], 1007)
expect_close("UTF-8 surrogate", [frame(1, b"\xed\xa0\x80")], 1007)
expect_close("overlong UTF-8", [frame(1, b"\xc0\xaf")], 1007)
expect_close("truncated UTF-8 at the end", [frame(1, b"ok\xe0\xa4")], 1007)
expect_close("close with a one-byte body", [frame(8, b"\x03")], 1002)
expect_close("close code 1005 on the wire", [frame(8, struct.pack(">H", 1005))], 1002)
expect_close("close code 999", [frame(8, struct.pack(">H", 999))], 1002)
expect_close("close code 2999", [frame(8, struct.pack(">H", 2999))], 1002)
expect_close("close reason not UTF-8", [frame(8, struct.pack(">H", 1000) + b"\xff")], 1007)
expect_close("valid close 1000 echoed", [frame(8, struct.pack(">H", 1000) + b"bye")], 1000)
expect_close("valid close 4999 echoed", [frame(8, struct.pack(">H", 4999))], 4999)
expect_close("valid close 1012 echoed", [frame(8, struct.pack(">H", 1012))], 1012)
expect_close("message over max_message (1 MiB here), refused from its header", [frame(2, b"", length=1024 * 1024 + 1)], 1009)
expect_close("fragments adding up to over max_message", [frame(2, b"z" * 600000, fin=False), frame(0, b"", length=600000)], 1009)
expect_close("length with the top bit set", [frame(2, b"", length=1 << 63)], 1002)

# empty close: answered with an empty close
s, head, _ = handshake()
s.sendall(frame(8, b""))
op, payload, _ = read_frame(s)
ok = op == 8 and payload == b""
print(("ok:   " if ok else "FAIL: ") + "empty close answered with an empty close"); fails += not ok
s.close()

# bad handshakes
for name, req, code in [
    ("wrong version -> 426", b"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 8\r\n\r\n", b"426"),
    ("no upgrade -> 400", b"GET / HTTP/1.1\r\nHost: x\r\n\r\n", b"400"),
    ("bad key -> 400", b"GET / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Key: short\r\nSec-WebSocket-Version: 13\r\n\r\n", b"400"),
    ("POST -> 400", b"POST / HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", b"400"),
]:
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    s.sendall(req)
    resp = s.recv(4096)
    ok = resp.startswith(b"HTTP/1.1 " + code)
    print(("ok:   " if ok else "FAIL: ") + name, resp.split(b"\r\n")[0])
    fails += not ok
    s.close()

# RFC 6455's own example key
s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
s.sendall(b"GET /chat HTTP/1.1\r\nHost: server.example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")
resp = s.recv(4096)
ok = b"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in resp
print(("ok:   " if ok else "FAIL: ") + "RFC 6455 example accept key s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")
fails += not ok
s.close()

print("python faults:", "all passed" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
