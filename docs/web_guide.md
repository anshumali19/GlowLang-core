# GlowLang Web Development Guide

A complete guide to building web servers, REST APIs, and web applications in GlowLang — using native HTTP, JSON, and concurrent request handling.

---

## Overview

GlowLang provides built-in HTTP server capabilities through its native `http` module. Key features:

- **Native TCP sockets** — no external dependencies
- **JSON encoding/decoding** — built into the runtime
- **Concurrent request handling** — via `spawn` for multi-client servers
- **Middleware pattern** — composable request processing

---

## 1. Hello World Server

### Minimal HTTP Server
```glow
show "Starting server on port 8080..."
let server = http.listen(8080)

let client = http.accept(server)
let request = http.read(client)
show "Received:", request

let body = "<h1>Hello from GlowLang!</h1>"
let response = "HTTP/1.1 200 OK\r\n"
response = response + "Content-Type: text/html\r\n"
response = response + "Content-Length: " + len(body) + "\r\n\r\n"
response = response + body

http.send(client, response)
http.close(client)
http.close(server)
show "Server stopped."
```

---

## 2. JSON API Server

### RESTful Endpoint
```glow
func json_response(status_code, data):
    let body = json.stringify(data)
    let status_text = "OK"
    when status_code == 404:
        status_text = "Not Found"
    eif status_code == 500:
        status_text = "Internal Server Error"
    end

    let response = "HTTP/1.1 " + status_code + " " + status_text + "\r\n"
    response = response + "Content-Type: application/json\r\n"
    response = response + "Content-Length: " + len(body) + "\r\n"
    response = response + "Connection: close\r\n\r\n"
    response = response + body
    return response
end

func handle_api(path):
    when path == "/":
        let data = {}
        data["message"] = "Welcome to GlowLang API"
        data["version"] = "1.0"
        return json_response(200, data)

    eif path == "/users":
        let data = {}
        data["users"] = ["Alice", "Bob", "Charlie"]
        data["count"] = 3
        return json_response(200, data)

    eif path == "/health":
        let data = {}
        data["status"] = "healthy"
        data["uptime"] = os.time()
        return json_response(200, data)

    otherwise:
        let data = {}
        data["error"] = "Not Found"
        data["path"] = path
        return json_response(404, data)
    end
end

# Run the server
show "API Server starting on port 3000..."
let server = http.listen(3000)

let count = 0
while count < 50:
    let client = http.accept(server)
    let raw = http.read(client)

    # Extract path from HTTP request
    let path = "/"
    show "Request received"

    let response = handle_api(path)
    http.send(client, response)
    http.close(client)
    count = count + 1
end

http.close(server)
```

---

## 3. Concurrent Web Server

### Multi-Threaded Request Handling
```glow
let routes = {}

func register_route(path, handler):
    routes[path] = handler
end

func serve_client(client):
    try:
        let request = http.read(client)
        when request is not none:
            show "Processing request..."

            # Parse the request path (simplified)
            let path = "/"

            when routes.has(path):
                let handler = routes[path]
                let body = handler(request)
                let response = "HTTP/1.1 200 OK\r\n"
                response = response + "Content-Type: application/json\r\n"
                response = response + "Content-Length: " + len(body) + "\r\n\r\n"
                response = response + body
                http.send(client, response)
            otherwise:
                let body = "{\"error\": \"Not Found\"}"
                let response = "HTTP/1.1 404 Not Found\r\n"
                response = response + "Content-Type: application/json\r\n"
                response = response + "Content-Length: " + len(body) + "\r\n\r\n"
                response = response + body
                http.send(client, response)
            end
        end
        http.close(client)
    catch e:
        show "Error handling client:", e
        http.close(client)
    end
end

# Register routes
func home_handler(req):
    let data = {}
    data["message"] = "Hello from concurrent GlowLang!"
    return json.stringify(data)
end

func status_handler(req):
    let data = {}
    data["status"] = "running"
    data["threads"] = "active"
    return json.stringify(data)
end

register_route("/", home_handler)
register_route("/status", status_handler)

# Start concurrent server
show "Concurrent server on port 8080..."
let server = http.listen(8080)

let running = true
let count = 0
while running:
    let client = http.accept(server)
    spawn serve_client(client)   # Handle each client in a new thread
    count = count + 1
    when count >= 100:
        running = false
    end
end

http.close(server)
show "Server shutdown"
```

---

## 4. Router Framework

### Blueprint-Based Router
```glow
blueprint Router:
    action init():
        self.routes = {}
        self.middlewares = []
    end

    action get(path, handler):
        self.routes[path] = handler
    end

    action use_middleware(mw):
        append(self.middlewares, mw)
    end

    action dispatch(path, request):
        # Run middlewares
        for each mw in self.middlewares:
            let ok = mw(request)
            when ok is not true:
                return "{\"error\": \"Blocked by middleware\"}"
            end
        end

        # Route to handler
        when self.routes.has(path):
            let handler = self.routes[path]
            return handler(request)
        otherwise:
            return "{\"error\": \"Route not found\"}"
        end
    end
end

# Usage
let app = Router()
app.init()

# Add logging middleware
func logger(req):
    show "[LOG] Request received"
    return true
end

app.use_middleware(logger)

# Add routes
func index(req):
    let data = {}
    data["page"] = "Home"
    return json.stringify(data)
end

func about(req):
    let data = {}
    data["page"] = "About GlowLang"
    data["version"] = "1.0"
    return json.stringify(data)
end

app.get("/", index)
app.get("/about", about)

# Test routing
show app.dispatch("/", "GET / HTTP/1.1")
show app.dispatch("/about", "GET /about HTTP/1.1")
show app.dispatch("/missing", "GET /missing HTTP/1.1")
```

---

## 5. JSON Handling

### Encoding
```glow
let user = {}
user["name"] = "Alice"
user["age"] = 30
user["active"] = true
user["tags"] = ["admin", "user"]

let json_str = json.stringify(user)
show json_str
# {"name":"Alice","age":30,"active":true,"tags":["admin","user"]}
```

### Decoding
```glow
let raw = "{\"name\": \"Bob\", \"score\": 95}"
let parsed = json.parse(raw)

show "Name:", parsed["name"]
show "Score:", parsed["score"]
```

### Building API Responses
```glow
func api_success(data):
    let response = {}
    response["status"] = "success"
    response["data"] = data
    response["timestamp"] = os.time()
    return json.stringify(response)
end

func api_error(message, code):
    let response = {}
    response["status"] = "error"
    response["message"] = message
    response["code"] = code
    return json.stringify(response)
end

show api_success({"users": 42})
show api_error("Not Found", 404)
```

---

## 6. Database-Backed API

### CRUD API with SQLite
```glow
func setup_db():
    let db = sqlite.open("api.db")
    sqlite.exec(db, "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, price REAL)")
    return db
end

func list_items(db):
    let rows = sqlite.query(db, "SELECT * FROM items")
    let data = {}
    data["items"] = rows
    data["count"] = len(rows)
    return json.stringify(data)
end

func add_item(db, name, price):
    let query = "INSERT INTO items (name, price) VALUES ('" + name + "', " + price + ")"
    sqlite.exec(db, query)
    let data = {}
    data["status"] = "created"
    data["name"] = name
    return json.stringify(data)
end

# Usage
let db = setup_db()

add_item(db, "Widget", 9.99)
add_item(db, "Gadget", 24.99)
add_item(db, "Gizmo", 14.99)

show list_items(db)

sqlite.close(db)
```

---

## 7. Middleware Patterns

### Authentication Middleware
```glow
func auth_middleware(request):
    # Check for API key in request
    show "[AUTH] Checking credentials..."
    # In a real app, parse headers from request
    return true   # Allow request
end

func rate_limit_middleware(request):
    show "[RATE] Checking rate limit..."
    return true   # Allow request
end

func cors_middleware(request):
    show "[CORS] Adding CORS headers..."
    return true
end
```

### Middleware Chain
```glow
let middleware_chain = [auth_middleware, rate_limit_middleware, cors_middleware]

func run_middlewares(chain, request):
    for each mw in chain:
        let result = mw(request)
        when result is not true:
            show "Request blocked by middleware"
            return false
        end
    end
    return true
end

let allowed = run_middlewares(middleware_chain, "GET /api/data")
show "Request allowed:", allowed
```

---

## 8. Best Practices

### Server Architecture
- ✅ Use `spawn` for each client connection — enables concurrent handling
- ✅ Wrap client handling in `try/catch` — prevents one client from crashing the server
- ✅ Close connections in `catch` blocks too — prevent resource leaks
- ✅ Use blueprints/classes for router organization

### JSON APIs
- ✅ Always set `Content-Type: application/json` header
- ✅ Include `Content-Length` for proper HTTP framing
- ✅ Use helper functions for consistent response formatting
- ✅ Return appropriate HTTP status codes

### Security
- ✅ Validate and sanitize all input before SQL queries
- ✅ Use middleware for authentication and rate limiting
- ✅ Close database connections after use
- ✅ Set connection timeouts for long-running requests

---

*GlowLang Web Development Guide v1.0*
