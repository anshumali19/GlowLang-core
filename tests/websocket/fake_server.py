"""A WebSocket server written only with Python's standard library that
misbehaves on purpose, to test the GlowLang client. The path picks what it
does. Plain on argv[1]; TLS on argv[2] with cert.pem / key.pem."""
import base64, hashlib, os, socket, ssl, struct, sys, threading

def frame(op, payload, fin=True, mask=False, rsv=0):
    b0 = (0x80 if fin else 0) | (rsv << 4) | op
    n = len(payload)
    mbit = 0x80 if mask else 0
    h = bytes([b0, mbit | n]) if n < 126 else (bytes([b0, mbit | 126]) + struct.pack(">H", n) if n < 65536 else bytes([b0, mbit | 127]) + struct.pack(">Q", n))
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
    if not b1 & 0x80:
        raise AssertionError("client did not mask")
    k = need(4)
    p = need(n)
    return b0 & 0x0F, bytes(c ^ k[i % 4] for i, c in enumerate(p))

def handle(s):
    try:
        req = b""
        while b"\r\n\r\n" not in req:
            c = s.recv(4096)
            if not c:
                return
            req += c
        lines = req.decode().split("\r\n")
        path = lines[0].split(" ")[1]
        headers = {l.split(":", 1)[0].lower(): l.split(":", 1)[1].strip() for l in lines[1:] if ":" in l}
        key = headers.get("sec-websocket-key", "")
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        if path == "/http404":
            s.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"); return
        if path == "/badaccept":
            s.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n\r\n"); return
        if path == "/badproto":
            s.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\nSec-WebSocket-Protocol: other\r\n\r\n").encode()); return
        if path == "/ext":
            s.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\nSec-WebSocket-Extensions: permessage-deflate\r\n\r\n").encode()); return
        resp = "HTTP/1.1 101 Switching Protocols\r\nupgrade: WebSocket\r\nconnection: upgrade\r\nsec-websocket-accept: " + accept + "\r\n"
        if "sec-websocket-protocol" in headers:
            resp += "Sec-WebSocket-Protocol: " + headers["sec-websocket-protocol"].split(",")[-1].strip() + "\r\n"
        resp += "\r\n"
        hello = b""
        if path == "/eager":
            # the first message arrives in the same packet as the handshake answer
            hello = frame(1, b"right away") + frame(9, b"pp") + frame(1, b"second")
        s.sendall(resp.encode() + hello)
        if path == "/masked":
            s.sendall(frame(1, b"masked", mask=True))
        elif path == "/badutf8":
            s.sendall(frame(1, b"\xff\xfe"))
        elif path == "/fragments":
            s.sendall(frame(1, b"frag", fin=False) + frame(9, b"!") + frame(0, b"ment", fin=False) + frame(0, b"ed"))
        elif path == "/servercloses":
            s.sendall(frame(8, struct.pack(">H", 1001) + "going away — bye".encode()))
            op, p = read_frame(s)
            s.sendall(b"" if op == 8 and p[:2] == struct.pack(">H", 1001) else b"")
            s.close(); return
        elif path == "/drop":
            s.sendall(frame(1, b"one"))
            s.shutdown(socket.SHUT_RDWR); s.close(); return
        elif path == "/headers":
            s.sendall(frame(1, ("auth=" + headers.get("authorization", "") + " ua=" + headers.get("user-agent", "") + " host=" + headers.get("host", "")).encode()))
        elif path == "/silent":
            pass
        # echo until close; checks every client frame is masked
        while True:
            op, p = read_frame(s)
            if op == 8:
                s.sendall(frame(8, p[:2]))
                break
            if op == 9:
                s.sendall(frame(10, p)); continue
            if op == 10:
                continue
            s.sendall(frame(op, p))
    except (EOFError, ConnectionResetError, BrokenPipeError, OSError):
        pass
    except AssertionError as e:
        print("fake server:", e, flush=True)
    finally:
        try: s.close()
        except OSError: pass

def serve(sock, wrap=None):
    while True:
        c, _ = sock.accept()
        if wrap:
            try:
                c = wrap.wrap_socket(c, server_side=True)
            except (ssl.SSLError, OSError):
                c.close(); continue
        threading.Thread(target=handle, args=(c,), daemon=True).start()

plain = socket.socket(); plain.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
plain.bind(("127.0.0.1", int(sys.argv[1]))); plain.listen(16)
tls = socket.socket(); tls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
tls.bind(("127.0.0.1", int(sys.argv[2]))); tls.listen(16)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain("cert.pem", "key.pem")
threading.Thread(target=serve, args=(tls, ctx), daemon=True).start()
print("ready", flush=True)
serve(plain)
