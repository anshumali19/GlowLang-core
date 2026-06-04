# GlowLang Deployment Guide

A practical guide to building, packaging, and deploying GlowLang applications — from local development to production environments, including WASM browser deployment.

---

## 1. Building from Source

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt install gcc flex bison libssl-dev libsqlite3-dev make

# Fedora/RHEL
sudo dnf install gcc flex bison openssl-devel sqlite-devel make

# macOS
brew install flex bison openssl sqlite
```

### Standard Build
```bash
cd GlowLang
make
```

This produces `./bin/glowlang` — the GlowLang runtime binary.

### Release Build (Optimized)
```bash
make CFLAGS="-O3 -march=native -flto -fomit-frame-pointer -DNDEBUG"
```

### Cross-Compilation
```bash
# ARM64 target (e.g., Raspberry Pi, Apple Silicon)
make CC=aarch64-linux-gnu-gcc

# WASM target
make -f Makefile.wasm
```

---

## 2. Project Structure

### Recommended Layout
```
my_app/
├── Glow.deps              # Dependencies
├── Glow.lock              # Lock file
├── src/
│   ├── main.glow          # Entry point
│   ├── routes.glow        # Route handlers
│   └── models.glow        # Data models
├── lib/
│   ├── utils.glow         # Utility functions
│   └── middleware.glow    # Middleware
├── tests/
│   ├── test_routes.glow   # Route tests
│   └── test_models.glow   # Model tests
├── config/
│   └── settings.glow      # Configuration
└── data/
    └── app.db             # SQLite database
```

### Entry Point
```glow
# src/main.glow
import "src/routes.glow"
import "src/models.glow"
import "lib/middleware.glow"

show "Application starting..."

# Initialize database
let db = setup_database()

# Start server
start_server(8080)
```

---

## 3. Package Management with gdu

### Declaring Dependencies
Create a `Glow.deps` file:
```
"https://github.com/user/web_framework.glow" "lib/web_framework.glow"
"https://github.com/user/auth_lib.glow" "lib/auth.glow"
"https://github.com/user/validation.glow" "lib/validation.glow"
```

### Installing Dependencies
```bash
# Install all dependencies
gdu install

# Add a new dependency
gdu add https://github.com/user/logger.glow

# List installed packages
gdu list
```

### Lock File
`Glow.lock` ensures reproducible builds:
```
"https://github.com/user/web_framework.glow" "lib/web_framework.glow" sha256:abc123...
"https://github.com/user/auth_lib.glow" "lib/auth.glow" sha256:def456...
```

Always commit `Glow.lock` to version control.

---

## 4. Running Applications

### Development Mode
```bash
# Run directly
./bin/glowlang src/main.glow

# With verbose output
./bin/glowlang src/main.glow --verbose
```

### Production Mode
```bash
# Run with optimizations
./bin/glowlang src/main.glow

# Run in background (Linux)
nohup ./bin/glowlang src/main.glow > app.log 2>&1 &

# Run with systemd
sudo systemctl start glowlang-app
```

---

## 5. Systemd Service

### Service File
Create `/etc/systemd/system/glowlang-app.service`:
```ini
[Unit]
Description=GlowLang Application
After=network.target

[Service]
Type=simple
User=glowapp
WorkingDirectory=/opt/my_app
ExecStart=/usr/local/bin/glowlang src/main.glow
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

### Managing the Service
```bash
sudo systemctl enable glowlang-app    # Enable on boot
sudo systemctl start glowlang-app     # Start
sudo systemctl status glowlang-app    # Check status
sudo systemctl stop glowlang-app      # Stop
sudo systemctl restart glowlang-app   # Restart
```

### Viewing Logs
```bash
journalctl -u glowlang-app -f         # Follow logs
journalctl -u glowlang-app --since today
```

---

## 6. Docker Deployment

### Dockerfile
```dockerfile
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    gcc flex bison libssl-dev libsqlite3-dev make

# Copy source and build
COPY . /build/GlowLang
WORKDIR /build/GlowLang
RUN make

# Runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libssl3 libsqlite3-0 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/GlowLang/bin/glowlang /usr/local/bin/
COPY . /app
WORKDIR /app

EXPOSE 8080
CMD ["glowlang", "src/main.glow"]
```

### Docker Compose
```yaml
version: "3.8"
services:
  app:
    build: .
    ports:
      - "8080:8080"
    volumes:
      - ./data:/app/data
    restart: always
    environment:
      - GLOW_ENV=production

  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
```

### Running
```bash
docker build -t my-glow-app .
docker run -p 8080:8080 my-glow-app

# With docker-compose
docker-compose up -d
```

---

## 7. WASM Deployment (Browser)

### Compiling to WebAssembly
```bash
make -f Makefile.wasm
```

This produces `glowlang.wasm` and `glowlang.js` loader.

### HTML Integration
```html
<!DOCTYPE html>
<html>
<head>
    <title>GlowLang WASM App</title>
</head>
<body>
    <div id="output"></div>
    <script src="glowlang.js"></script>
    <script>
        GlowLang.init().then(runtime => {
            runtime.execute(`
                show "Hello from GlowLang in the browser!"
                let result = 42 * 2
                show f"The answer is {result}"
            `);
        });
    </script>
</body>
</html>
```

---

## 8. Reverse Proxy (Nginx)

### Nginx Configuration
```nginx
server {
    listen 80;
    server_name myapp.example.com;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

### With SSL (Let's Encrypt)
```bash
sudo certbot --nginx -d myapp.example.com
```

---

## 9. Health Checks and Monitoring

### Health Check Endpoint
```glow
func health_handler(req):
    let status = {}
    status["status"] = "healthy"
    status["uptime"] = os.time()
    status["version"] = "1.0.0"
    return json.stringify(status)
end

# Register at /health
register_route("/health", health_handler)
```

### Monitoring Script
```bash
#!/bin/bash
# health_check.sh
while true; do
    response=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8080/health)
    if [ "$response" != "200" ]; then
        echo "ALERT: Service unhealthy (HTTP $response)"
        systemctl restart glowlang-app
    fi
    sleep 30
done
```

---

## 10. Database Migration Strategy

### Schema Versioning
```glow
func migrate(db):
    # Check current schema version
    sqlite.exec(db, "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER)")
    let rows = sqlite.query(db, "SELECT version FROM schema_version")

    let current = 0
    when len(rows) > 0:
        current = rows[0]["version"]
    end

    # Apply migrations
    when current < 1:
        show "Applying migration v1..."
        sqlite.exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)")
        sqlite.exec(db, "INSERT INTO schema_version VALUES (1)")
    end

    when current < 2:
        show "Applying migration v2..."
        sqlite.exec(db, "CREATE TABLE posts (id INTEGER PRIMARY KEY, user_id INTEGER, title TEXT, body TEXT)")
        sqlite.exec(db, "UPDATE schema_version SET version = 2")
    end

    show f"Database at version {current}"
end
```

---

## 11. Production Checklist

### Before Deploying
- [ ] All tests pass locally
- [ ] `Glow.lock` is committed
- [ ] Error handling covers all code paths
- [ ] Database migrations are idempotent
- [ ] Health check endpoint is functional
- [ ] Logging captures errors with context
- [ ] Resource limits are configured (memory, connections)

### Performance Tuning
- [ ] Hot functions are reaching JIT threshold
- [ ] Tensor operations use `tensor.*` builtins (not manual loops)
- [ ] Database connections are properly closed
- [ ] HTTP connections use `Connection: close` or keep-alive appropriately

### Security
- [ ] Input validation on all user-facing endpoints
- [ ] SQL queries use parameterized values where possible
- [ ] API keys and secrets are not hardcoded
- [ ] HTTPS is configured via reverse proxy

---

*GlowLang Deployment Guide v1.0*
