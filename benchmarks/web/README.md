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

RESULTS_PLACEHOLDER

FastAPI is not in the table: it needs `pip`, which this machine's Python
does not have, and installing it would have meant changing the system.
`run.sh` is easy to extend with any other server that answers on a port.
