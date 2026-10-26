# GlowLang Production ROI — Developer Guide

Welcome to the hardened, professional GlowLang ecosystem. This guide covers the new tooling and ecosystem features implemented for industrial production.

## 1. Professional Tooling

### VM Tracing (GlowStack)
You can now visualize the VM execution frame-by-frame using the `trace` subcommand.
```bash
glow trace script.glow
```
This will output the PC, OpCode, and the state of all registers for every instruction.

### Standalone Packaging
Deploy your apps as single binary files (self-contained runtime + script).
```bash
glow package main.glow -o myapp
./myapp
```

### Performance Profiling
Integrated hot-path profiling.
```glow
debug.profile_start()
# ... heavy computation ...
debug.profile_stop()
```

## 2. Ecosystem Frameworks

### GlowWeb (Native Web)
High-performance, async-first web framework.
```glow
import "lib/web.glow"
let app = Server(8080)
app.route("/", lambda(req): "Hello World")
app.start()
```

### GlowORM (Database)
Simple, blueprint-mapped database interactions.
```glow
import "lib/db.glow"
let orm = Model("app.db")
orm.create_table("tasks", {"title": "TEXT"})
orm.insert("tasks", {"title": "Finish ROI Phase"})
```

### GlowAI (Tensor/Matrix)
SIMD-accelerated AI and matrix operations.
```glow
import "lib/ai.glow"
let net = SimpleNet()
let prediction = net.predict(tensor.zeros(10))
```

## 3. Runtime behaviour you can rely on
- **Garbage collection:** non-moving mark-and-sweep; collects when allocation passes max(32 MB, 2 × live data). Stop-the-world.
- **JIT:** x86-64 (Windows x64 and System V ABIs). Hot functions and loops are compiled; anything compiled code cannot handle continues in the interpreter with the same result.
- **Concurrency:** `spawn` runs OS threads under a global interpreter lock. It helps with waiting (sleep, sockets, HTTP), not with CPU-bound work. Use `threads.join_all()` to wait for workers.
- **Cryptography:** OpenSSL SHA-256 and HMAC-SHA256 (`crypto.sha256`, `crypto.hmac`).
- **Recursion:** tail calls reuse frames; other recursion is limited to 65,500 calls and raises a catchable error instead of crashing.
- **Crash reports (Windows):** a symbolized native backtrace when `glowlang.pdb` is beside the executable.
- **Packaged binaries** (`glow package`) are copies of the runtime and are not signed; sign them before distribution.
