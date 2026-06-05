# GlowLang Production Deployment Guide

## Overview

GlowLang supports multiple deployment strategies for production applications:
- **Standalone binary** — Direct execution on any Linux x86_64 system
- **Docker** — Containerized deployment with multi-stage builds
- **Docker Compose** — Multi-service deployments (app + worker)
- **WASM** — Browser-based execution
- **ARM64** — Cross-compiled for ARM64 servers (AWS Graviton, etc.)

## Quick Start

### Build from Source
```bash
# Prerequisites: gcc, flex, bison, libsqlite3-dev, libssl-dev, libfl-dev, libx11-dev
make all
./bin/glowlang your_app.glow
```

### Docker Deployment
```bash
cd deploy
docker build -t glowlang-app -f Dockerfile ..
docker run -p 8080:8080 glowlang-app examples/web_api.glow
```

### Docker Compose (App + Worker)
```bash
cd deploy
docker-compose up -d
```

## Application Structure

A production GlowLang application should follow this layout:
```
my-app/
├── Glow.deps          # Dependencies
├── Glow.lock          # Locked versions
├── lib/               # Library code
│   ├── routes.glow
│   ├── models.glow
│   └── helpers.glow
├── main.glow          # Entry point
├── tests/             # Test files
│   └── test_app.glow
└── deploy/            # Deployment configs
    └── Dockerfile
```

## Database Configuration

GlowLang uses SQLite for embedded database operations:
```
# In your app:
let db = sqlite.open("production.db")
sqlite.exec(db, "CREATE TABLE IF NOT EXISTS ...")
```

For production, use WAL mode for concurrent reads:
```
sqlite.exec(db, "PRAGMA journal_mode=WAL")
sqlite.exec(db, "PRAGMA busy_timeout=5000")
```

## HTTP Server Deployment

```
# Production web server
let server = http.listen(8080)
show "Server running on port 8080"

while true:
    let client = http.accept(server)
    spawn handle_request(client)
end
```

## Environment Variables

Access environment variables with `os.env()`:
```
let port = os.env("PORT")
let db_path = os.env("DB_PATH")
```

## Health Checks

Implement health check endpoints for container orchestration:
```
func health_check(request):
    return '{"status":"healthy","uptime":' + os.time() + '}'
end
```

## Monitoring

Use the built-in profiler for production monitoring:
```bash
./bin/glow_profiler --output profile_report.html
```

## Security Considerations

1. **Input Validation**: Always validate user input before database queries
2. **SQL Injection**: Use parameterized queries where possible
3. **Resource Limits**: Set memory limits in Docker deployments
4. **Non-root**: Docker images run as non-root user by default

## Scaling

- **Horizontal**: Deploy multiple Docker containers behind a load balancer
- **Vertical**: Increase container resource limits
- **Worker Pattern**: Use Docker Compose with separate worker services
- **Connection Pooling**: Reuse SQLite connections within a process
