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
app.route("/", func(req): return "Hello World" end)
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

## 3. Production Hardening & Reliability
GlowLang is engineered with production-grade runtime guarantees:
- **Generational GC:** 2 MB young generation nursery (Gen 0) triggering minor scavenge/promotion cycles, alongside an 8 MB tenured heap (Gen 1) swept via mark-and-sweep.
- **Dual-ABI x86-64 Native JIT:** Full compliance with Microsoft x64 ABI (Windows) and System V AMD64 ABI (Linux/macOS), featuring monomorphic inline caching for integer/float arithmetic and 16-byte aligned recursive dispatch.
- **Multi-Core Concurrency:** Native OS thread execution via `spawn` with isolated VM register stacks — the Global Interpreter Lock (GIL) has been abolished for true multi-core parallelization.
- **Hardware MMU Memory Protection:** `glow_alloc_guarded` allocates memory flanked by hardware CPU MMU guard pages (`PAGE_NOACCESS` / `PROT_NONE`), trapping buffer overruns instantly at the hardware level.
- **Cryptographic Security:** Standard OpenSSL 3.x HMAC-SHA256 and SHA-256 primitives (`crypto.hmac`, `crypto.sha256`) replacing insecure toy hashes.
- **Stack-Safe Recursion:** Tail-Call Optimization (TCO) and bounded frame recursion checks.
