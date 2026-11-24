"""Talks to a GlowWeb server over raw sockets to check HTTP keep-alive.
Used by tests/test_keep_alive.glow:  python3 keep_alive_check.py PORT
Prints one line per check."""
import socket
import sys
import time

PORT = int(sys.argv[1])


def connect():
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    return s, s.makefile("rb")


def response(f):
    """One response: (status line, headers dict, body), or None at EOF."""
    line = f.readline()
    if not line:
        return None
    headers = {}
    while True:
        h = f.readline().decode().strip()
        if not h:
            break
        k, v = h.split(":", 1)
        headers[k.strip().lower()] = v.strip()
    body = f.read(int(headers.get("content-length", "0")))
    return line.decode().strip(), headers, body.decode()


def get(path, version="HTTP/1.1", extra=""):
    return f"GET {path} {version}\r\nHost: x\r\n{extra}\r\n".encode()


# Three requests, one connection.
s, f = connect()
for i in range(3):
    s.sendall(get(f"/n/{i}"))
    status, h, body = response(f)
    print("same connection:", status, h.get("connection"), body)
body = "hello there"
s.sendall(f"POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: {len(body)}\r\n\r\n{body}".encode())
status, h, got = response(f)
print("post on it:", status, got)
s.sendall(get("/n/9", extra="Connection: close\r\n"))
status, h, got = response(f)
print("asked to close:", h.get("connection"), got, "then", response(f))
s.close()

# HTTP/1.0 closes unless asked; with keep-alive it stays open.
s, f = connect()
s.sendall(get("/n/10", "HTTP/1.0"))
print("1.0:", response(f)[1].get("connection"), response(f))
s.close()
s, f = connect()
for i in (11, 12):
    s.sendall(get(f"/n/{i}", "HTTP/1.0", "Connection: keep-alive\r\n"))
    status, h, got = response(f)
    print("1.0 keep-alive:", h.get("connection"), got)
s.close()

# Two requests in one write: the first is answered, then the connection
# closes (the second was read with it and is not lost silently).
s, f = connect()
s.sendall(get("/n/20") + get("/n/21"))
status, h, got = response(f)
print("pipelined:", got, h.get("connection"), "then", response(f))
s.close()

# A handler that sets Connection: close wins.
s, f = connect()
s.sendall(get("/bye"))
status, h, got = response(f)
print("handler closes:", h.get("connection"), got, "then", response(f))
s.close()

# An idle connection is closed by the server after 5 seconds.
s, f = connect()
s.sendall(get("/n/30"))
response(f)
start = time.time()
end = response(f)
waited = time.time() - start
print("idle:", end, 4 <= waited < 8)
s.close()
