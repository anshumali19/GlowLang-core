# Web server benchmark

The same small JSON API, `GET /notes/:id` returning
`{"id": 42, "text": "buy milk", "done": false}`, served three ways:

- [glowweb_server.glow](glowweb_server.glow): GlowLang's GlowWeb
- [python_server.py](python_server.py): Python's standard library (`ThreadingHTTPServer`)
- [node_server.js](node_server.js): Node.js's built-in `http` module

[load.py](load.py) sends requests from 1 or 8 processes at once, each over a
new connection (GlowWeb closes every connection, so all three are measured
the same way), and reports requests per second and the median and 99th
percentile time per request.

```bash
benchmarks/web/run.sh [path/to/glowlang] [seconds per run]
```

## Results

Measured on 2026-11-24 with `run.sh` (5 seconds per run) on an Intel
i7-14650HX, Linux under WSL 2, GlowLang at main after 1.11.0, Python
3.14.4, Node.js 22.22.1. Three runs agreed within 3%; this is the middle
one.

| Server | Clients | Requests/s | Median | p99 |
|---|---|---|---|---|
| GlowLang (GlowWeb) | 1 | 11,372 | 0.08 ms | 0.19 ms |
| GlowLang (GlowWeb) | 8 | 9,324 | 0.84 ms | 1.30 ms |
| Python (http.server) | 1 | 3,531 | 0.28 ms | 0.44 ms |
| Python (http.server) | 8 | 3,532 | 1.68 ms | 2.30 ms |
| Node.js (http) | 1 | 11,041 | 0.08 ms | 0.23 ms |
| Node.js (http) | 8 | 16,588 | 0.45 ms | 0.85 ms |

With one client GlowWeb and Node answer at the same rate, about three
times Python's. With 8 clients Node pulls ahead: GlowWeb runs a thread per
connection and the threads share the GIL, so more clients do not add
throughput.

FastAPI is not in the table: it needs `pip`, which this machine's Python
does not have, and installing it would have meant changing the system.
`run.sh` is easy to extend with any other server that answers on a port.
