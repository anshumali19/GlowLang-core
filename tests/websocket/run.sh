#!/bin/bash
# WebSocket checks against other implementations, run from the repository
# root after `make` (CI job "websocket"). Needs python3, node 22+ (for its
# built-in WebSocket), openssl and docker (for the Autobahn test suite).
#   1. Node's WebSocket client against the GlowLang server
#   2. malformed frames and handshakes (Python) against the GlowLang server
#   3. the GlowLang client against a misbehaving Python server, plain and TLS
#   4. Autobahn: fuzzing client -> GlowLang server, fuzzing server -> GlowLang client
set -u
ROOT=$(pwd)
BIN=${GLOWLANG_BIN:-$ROOT/bin/glowlang}
W=$(mktemp -d)
cp tests/websocket/* "$W/"
cd "$W"
status=0
pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT
wait_port() { for i in $(seq 1 50); do (echo > /dev/tcp/127.0.0.1/$1) 2>/dev/null && return 0; sleep 0.1; done; return 1; }

echo "== 1. Node client -> GlowLang server"
"$BIN" run node_server.glow 29981 </dev/null & pids+=($!)
wait_port 29981 && node node_client.mjs 29981 || status=1

echo "== 2. malformed frames -> GlowLang server"
"$BIN" run fault_server.glow 29982 </dev/null & pids+=($!)
wait_port 29982 && python3 fault_client.py 29982 || status=1

echo "== 3. GlowLang client -> misbehaving server (plain and TLS)"
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 2 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" 2>/dev/null
python3 fake_server.py 29983 29984 & pids+=($!)
wait_port 29983 && wait_port 29984
out=$("$BIN" run client_faults.glow 29983 29984 </dev/null 2>&1)
echo "$out"
echo "$out" | grep -q "failed: 0$" || status=1

if [ "${SKIP_AUTOBAHN:-0}" != "1" ]; then
    IMAGE=crossbario/autobahn-testsuite:0.8.2
    mkdir -p reports
    echo "== 4a. Autobahn fuzzing client -> GlowLang server"
    "$BIN" run autobahn_echo.glow 9001 </dev/null & pids+=($!)
    wait_port 9001
    docker run --rm --network host -v "$W:/config" -v "$W/reports:/reports" "$IMAGE" \
        wstest -m fuzzingclient -s /config/fuzzingclient.json > autobahn_client.log 2>&1 || { tail -30 autobahn_client.log; status=1; }
    python3 check_autobahn.py reports/servers/index.json GlowLang || status=1

    echo "== 4b. Autobahn fuzzing server -> GlowLang client"
    docker run -d --name autobahn-server --network host -v "$W:/config" -v "$W/reports:/reports" "$IMAGE" \
        wstest -m fuzzingserver -s /config/fuzzingserver.json > /dev/null
    wait_port 9002
    "$BIN" run autobahn_client.glow ws://127.0.0.1:9002 </dev/null | tail -5
    docker rm -f autobahn-server > /dev/null
    python3 check_autobahn.py reports/clients/index.json GlowLang || status=1
    [ -n "${REPORT_DIR:-}" ] && cp -r reports "$REPORT_DIR/"
fi

echo
[ $status = 0 ] && echo "websocket checks: all passed" || echo "websocket checks: FAILED"
exit $status
