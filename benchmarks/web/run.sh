#!/usr/bin/env bash
# Compare the same JSON API served by GlowWeb, Python's standard library and
# Node's http module: requests per second and time per request, with 1 and
# 8 clients sending at once.
#   benchmarks/web/run.sh [path/to/glowlang] [seconds per run]
# Needs python3 and node. Every server runs as one process on this machine.
set -u
cd "$(dirname "$0")"
GLOW="${1:-../../bin/glowlang}"
SECS="${2:-5}"

wait_up() {   # port
    for _ in $(seq 1 100); do
        python3 -c "import socket,sys; socket.create_connection(('127.0.0.1', int(sys.argv[1]))).close()" "$1" 2>/dev/null && return 0
        sleep 0.1
    done
    echo "server on port $1 did not start" >&2; return 1
}

run() {   # name port command...
    local name="$1" port="$2"; shift 2
    PORT=$port "$@" >/dev/null 2>&1 &
    local pid=$!
    wait_up "$port" || { kill $pid 2>/dev/null; return; }
    python3 load.py "$port" 1 4 >/dev/null          # warm up
    for c in 1 8; do
        printf '%-26s %2s clients  %s\n' "$name" "$c" "$(python3 load.py "$port" "$SECS" "$c")"
    done
    kill $pid 2>/dev/null; wait $pid 2>/dev/null || true
}

run "GlowLang (GlowWeb)" 18081 "$GLOW" run glowweb_server.glow
run "Python (http.server)" 18082 python3 python_server.py
run "Node.js (http)" 18083 node node_server.js
exit 0
