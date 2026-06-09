# GlowWeb Tutorial — Web Development in GlowLang

## Introduction

GlowWeb provides HTTP server capabilities built on GlowLang's native `http.*` API.
It supports routing, request handling, and concurrent client processing via `spawn`.

## Quick Start

### Hello World Server
```
let server = http.listen(8080)
show "Server running on port 8080"

while true:
    let client = http.accept(server)
    when client >= 0:
        let request = http.read(client)
        http.send(client, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello from GlowLang!")
        http.close(client)
    end
end
```

### Using the GlowWeb Library
```
use "lib/web.glow"

let app = Server(8080)

app.route("/", lambda(req): "<h1>Welcome to GlowLang</h1>")
app.route("/api/health", lambda(req): '{"status":"ok"}')

app.start()
```

## HTTP API Reference

### Core Functions
| Function | Description |
|----------|-------------|
| `http.listen(port)` | Create a TCP listener on the given port |
| `http.accept(server)` | Accept an incoming connection (blocking) |
| `http.read(client)` | Read the raw HTTP request as a string |
| `http.send(client, data)` | Send response data to the client |
| `http.close(handle)` | Close a server or client connection |
| `http.request(method, url, headers, body)` | Make an outbound HTTP request |

### Building Responses
```
func send_json(client, status, data):
    let body = json.stringify(data)
    let response = "HTTP/1.1 " + status + "\r\n"
    response = response + "Content-Type: application/json\r\n"
    response = response + "Content-Length: " + len(body) + "\r\n"
    response = response + "\r\n"
    response = response + body
    http.send(client, response)
end
```

### Parsing Requests
```
func parse_request(raw):
    let result = {}
    let lines = raw.split("\n")
    when len(lines) > 0:
        let first_line = lines[0].split(" ")
        when len(first_line) >= 2:
            result["method"] = first_line[0]
            result["path"] = first_line[1]
        end
    end
    return result
end
```

## REST API Pattern
```
let db = sqlite.open("api.db")
sqlite.exec(db, "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, price REAL)")

func handle_request(client):
    let raw = http.read(client)
    let req = parse_request(raw)
    
    when req["path"] == "/api/items":
        let rows = sqlite.query(db, "SELECT * FROM items")
        send_json(client, "200 OK", rows)
    otherwise:
        send_json(client, "404 Not Found", {"error": "not found"})
    end
    
    http.close(client)
end

let server = http.listen(8080)
while true:
    let client = http.accept(server)
    when client >= 0:
        spawn handle_request(client)
    end
end
```

## Concurrent Request Handling

GlowLang processes requests concurrently using `spawn`:
```
func handle_client(client):
    let request = http.read(client)
    # Process request...
    let response = "HTTP/1.1 200 OK\r\n\r\nOK"
    http.send(client, response)
    http.close(client)
end

let server = http.listen(8080)
while true:
    let client = http.accept(server)
    spawn handle_client(client)
end
```

## JSON API Helpers
```
# Parse JSON request body
let data = json.parse(request_body)

# Build JSON response
let response = json.stringify({"status": "ok", "count": 42})
```

## Best Practices

1. **Always close connections** — use `http.close(client)` after sending response
2. **Use spawn for concurrency** — handle each client in its own thread
3. **Set Content-Length** — always include Content-Length header for proper HTTP
4. **Error handling** — wrap request handlers in try/catch
5. **Database connections** — share a single SQLite connection (WAL mode for concurrency)
6. **Timeouts** — implement request timeouts for production stability
