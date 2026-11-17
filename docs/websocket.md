# WebSockets

Two-way connections that stay open: chat, live dashboards, notifications,
multiplayer games, talking to exchanges and IoT devices. GlowLang speaks
WebSocket (RFC 6455) as a client (`ws://` and `wss://`) and as a server.

```glow
use "websocket"
```

## Client

```glow
let ws = websocket.connect("wss://stream.example.com/prices")
ws.send_json({"subscribe": ["BTC-INR"]})
while true:
    let msg = ws.receive()
    when msg["type"] == "close":
        show "closed:", msg["code"], msg["reason"]
        stop
    end
    show json.parse(msg["data"])
end
```

`websocket.connect(url, options)` options:

| Option | Default | |
|---|---|---|
| `headers` | — | Extra request headers, e.g. `{"Authorization": "Bearer ..."}` |
| `protocols` | — | Subprotocols to offer, e.g. `["graphql-transport-ws"]`; `ws.protocol` is the one the server chose |
| `timeout` | 30 | Seconds to connect and finish the handshake |
| `verify` | `true` | Check the server's TLS certificate (`false` only for testing) |
| `max_message` | 16 MiB | Largest message accepted; a bigger one closes the connection with 1009 |
| `send_timeout` | 30 | Seconds a send may wait for a peer that does not read |

## A connection

| | |
|---|---|
| `ws.send(text)` | A text message (numbers are sent as text) |
| `ws.send_bytes(data)` | A binary message |
| `ws.send_json(value)` | A dict or list as JSON text |
| `ws.receive()` | The next message, waiting as long as it takes |
| `ws.receive(seconds)` | The next message, or `none` if nothing arrived in time |
| `ws.receive_json(seconds)` | The next message parsed as JSON |
| `ws.ping(data)` | Checks the connection; the pong is handled by `receive` |
| `ws.close(code = 1000, reason = "")` | Closes, waiting briefly for the other side to agree |
| `ws.is_open()`, `ws.protocol` | |

A message is a dict:

```glow
{"type": "text", "data": "hello"}
{"type": "binary", "data": <bytes>}
{"type": "close", "code": 1000, "reason": "bye"}
```

Once a connection has ended, `receive()` returns its close every time, and
`send` raises. The close code tells why it ended: 1000 normal, 1001 going
away, 1002 protocol error, 1006 the connection dropped without a close,
1007 a text message that was not UTF-8, 1009 a message over `max_message`,
1011 a server error, 3000-4999 your own application's codes.

Pings from the other side are answered inside `receive`, so keep a thread
receiving (most programs do anyway). One thread can receive while others
send on the same connection: each message goes out whole.

## Server

```glow
use "websocket"

let clients = []

func chat(ws):
    clients.push(ws)
    while true:
        let msg = ws.receive()
        when msg["type"] == "close":
            stop
        end
        for each other in clients:
            when other.is_open():
                other.send(msg["data"])
            end
        end
    end
    clients.remove(ws)
end

websocket.serve(8765, chat)
```

`websocket.serve(port, handler, options)` runs `handler(ws)` in a thread for
each connection and closes the connection when the handler returns (with
1011 if it raised). Options: `protocols` (the subprotocols this server
speaks, preferred first), `max_message`, `send_timeout`.

With GlowWeb, WebSocket routes sit next to ordinary ones:

```glow
import "lib/web.glow"

func live(ws):
    ws.send("hello " + ws.request.query_params.get("name", "there"))
    while ws.receive()["type"] != "close":
        ws.send("tick")
    end
end

let app = Server(8080)
app.get("/", home)
app.websocket("/live", live)     # ws://host:8080/live?name=Asha
app.start()
```

`ws.request` is the HTTP request that opened the connection (path, query,
headers: check cookies or tokens there). A request to a WebSocket path that
is not an upgrade goes to the ordinary routes.

For your own accept loop, `websocket.accept(client, request, options)`
takes a connection from `http.accept` (and the text `http.read` returned,
or `none`). A request that is not a WebSocket upgrade is answered with 400
(426 for another protocol version) and raises.

The server speaks plain `ws://`. For `wss://`, run it behind a TLS proxy
such as nginx or Caddy, which also handles certificates:

```nginx
location /live {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_read_timeout 1h;
}
```

## What is supported

- RFC 6455 framing: fragmented messages, pings and pongs between
  fragments, close handshakes with codes and reasons, masking (clients
  mask, servers refuse unmasked frames), 7-, 16- and 64-bit lengths.
- UTF-8 is checked on every text message and close reason, as it arrives.
- Protocol errors close the connection with the right code (1002, 1007,
  1009) rather than being ignored.
- Not supported: compression (permessage-deflate) and other extensions,
  which are simply not negotiated; servers fall back to uncompressed.

## Tested against

- `tests/test_websocket.glow` (38 checks: sizes, binary, threads, 20
  clients at once, subprotocols, limits, closing) and
  `tests/test_websocket_web.glow`.
- `tests/websocket/` (run in CI):
  - Node's built-in WebSocket client against the GlowLang server;
  - a Python client sending 35 kinds of malformed frames and handshakes to
    the server, checking each close code;
  - the GlowLang client against a Python server that misbehaves on purpose
    (masked frames, bad UTF-8, wrong handshakes, dropped connections), over
    plain TCP and TLS;
  - the Autobahn test suite, the standard conformance tests for WebSocket
    implementations, against both the server and the client.
- Every check also runs interpreter-only, under GC stress and under
  AddressSanitizer.
