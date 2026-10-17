<p align="center">
  <h1 align="center">🔥 GlowLang</h1>
  <p align="center"><strong>A programming language with natural-language syntax, SIMD tensors, built-in HTTP, SQLite bindings, a bytecode VM with JIT, and a growing standard library and tooling.</strong></p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C-blue" alt="Language">
  <img src="https://img.shields.io/badge/tests-117%2F117%20passing-brightgreen" alt="Tests">
  <img src="https://img.shields.io/badge/license-Sovereign-blue" alt="License">
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows%20x64%20%7C%20WASM-lightgrey" alt="Platform">
</p>

---

## ✨ Features

| Category | Capabilities |
|----------|-------------|
| **Natural Syntax** | `show`, `when`, `whenever`, `every`, `blueprint`, `action`, `can`, `use` |
| **Runtime** | Register-based VM, Microsoft x64 & SysV JIT, Generational GC (minor bump nursery + major mark-sweep), bytecode optimizer |
| **Concurrency** | Multi-core `spawn` parallel execution (GIL abolished, thread-local isolates) |
| **HTTP Server** | Built-in TCP server with `http.listen`, `http.accept`, `http.send` |
| **Database** | Native SQLite with `sqlite.open`, `sqlite.exec`, `sqlite.query` |
| **Tensors** | Hardware SIMD-accelerated (AVX2/FMA `_mm256_fmadd_pd`) via `tensor.create`, `tensor.zeros`, `tensor.ones`, `tensor.matmul` |
| **Security** | Hardware MMU page guards (`PAGE_NOACCESS`), unsigned array bounds validation, OpenSSL SHA-256 & HMAC |
| **AI/ML** | Neural net blueprints, matrix ops, activation functions, training loops |
| **JSON** | Native `json.parse` and `json.stringify` |
| **Filesystem** | `fs.read`, `fs.write`, `fs.exists`, `fs.mkdir` |
| **Package Manager** | `gdu` (Glow Dependency Utility) with semantic versioning, dependency resolution, SHA-256 checksums |
| **LSP** | Language Server Protocol with completion, hover, go-to-definition, diagnostics |
| **Editor Support** | VSCode, Neovim, Sublime Text, Helix, JetBrains |
| **Tooling** | Debugger, profiler, heap inspector, bytecode viewer |
| **Testing** | 117-case test suite (100% passing), fuzz harnesses, ASan/TSan builds |

## 🚀 Quick Start

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install gcc flex bison libsqlite3-dev libssl-dev libfl-dev libx11-dev make

# Build
git clone https://github.com/anshumali19/GlowLang.git
cd GlowLang
make all

# Run
./bin/glowlang examples/hello.glow
```

## 📝 Hello World

```
show "Hello, World!"

let name = "GlowLang"
show "Welcome to " + name + "!"

# Natural language loops
every item in [1, 2, 3, 4, 5]:
    show "Item: " + item
end
```

## 🏗️ Language Highlights

### Natural Syntax Classes
```
blueprint Animal:
    action init(name, sound):
        self.name = name
        self.sound = sound
    end

    action speak():
        show self.name + " says " + self.sound
    end
end

let dog = Animal("Rex", "Woof")
dog.speak()
```

### HTTP Server
```
let server = http.listen(8080)
show "Server on :8080"

while true:
    let client = http.accept(server)
    let req = http.read(client)
    http.send(client, "HTTP/1.1 200 OK\r\n\r\nHello from GlowLang!")
    http.close(client)
end
```

### Database (SQLite)
```
let db = sqlite.open("app.db")
sqlite.exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
sqlite.exec(db, "INSERT INTO users (name, age) VALUES ('Alice', 30)")
let rows = sqlite.query(db, "SELECT * FROM users")
for each row in rows:
    show row["name"] + " is " + row["age"]
end
```

### SIMD Tensors
```
let a = tensor.zeros(1000000)
let b = tensor.zeros(1000000)
let c = tensor.add(a, b)        # AVX2 accelerated
let dot = tensor.dot(a, b)      # SIMD dot product
```

### Concurrency
```
func worker(id):
    show "Worker " + id + " running"
end

spawn worker(1)
spawn worker(2)
os.sleep(1000)
```

## 📦 Project Structure

```
GlowLang/
├── src/                    # Core runtime (C)
│   ├── glowlang.y          # Parser (Bison)
│   ├── glowlang.l          # Lexer (Flex)
│   ├── vm.c                # Virtual machine
│   ├── compiler.c          # Bytecode compiler
│   ├── jit.c               # JIT compiler
│   ├── gc.c                # Garbage collector
│   ├── stdlib.c            # Standard library
│   └── lsp.c               # Language Server Protocol
├── lib/                    # GlowLang standard libraries
│   ├── web.glow            # HTTP framework
│   ├── orm.glow            # ORM layer
│   ├── ai.glow             # Neural networks
│   ├── tensor_utils.glow   # Tensor operations
│   ├── db.glow             # Database toolkit
│   └── ...
├── tests/                  # Test suite (.glow cases + C fuzz harnesses)
├── examples/               # Example applications
├── editors/                # Editor integrations
├── tools/                  # Dev tools (debugger, profiler)
├── deploy/                 # Docker & deployment configs
├── docs/                   # Documentation
└── Makefile
```

## 🧪 Testing

```bash
# Run the full test suite
make verify

# Run with AddressSanitizer + ThreadSanitizer
make test-sanitized

# Run all fuzz harnesses
make fuzz-all

# Build standalone tools
make tools
```

## 📖 Documentation

- **[Setup & Deploy Guide](docs/SETUP_AND_DEPLOY.md)** — Installation, editor setup, project creation
- **[Language Reference](docs/language_reference.md)** — Complete syntax reference
- **[Getting Started](docs/getting_started.md)** — Tutorial for beginners
- **[Web Development](docs/web_guide.md)** — HTTP server & APIs
- **[AI/ML Guide](docs/ai_ml_guide.md)** — Tensors & neural networks
- **[Concurrency Guide](docs/concurrency_guide.md)** — Spawn, threads, async patterns
- **[Runtime Internals](docs/runtime_internals.md)** — VM, JIT, GC architecture
- **[Production Deployment](docs/production_deployment.md)** — Docker, ARM64, CI/CD

## 🛠️ Editor Support

| Editor | Status | Location |
|--------|--------|----------|
| **VSCode** | ✅ Extension with syntax + snippets | `editors/glowlang-vscode/` |
| **Neovim** | ✅ LSP config + syntax | `editors/neovim/` |
| **Sublime Text** | ✅ Syntax definition | `editors/sublime/` |
| **Helix** | ✅ Language config + highlights | `editors/helix/` |
| **JetBrains** | ✅ File type definition | `editors/jetbrains/` |

## 📊 Architecture

```
Source Code (.glow)
    ↓
  Lexer (Flex) → Tokens
    ↓
  Parser (Bison) → AST
    ↓
  Compiler → Bytecode
    ↓
  Optimizer → Optimized Bytecode
    ↓
  VM (Safe Path) ←→ JIT (Fast Path)
    ↓
  GC (Generational, Concurrent)
```

## 🤝 Contributing

1. Fork the repository
2. Create your feature branch: `git checkout -b feature/amazing`
3. Run tests: `make verify`
4. Commit: `git commit -m 'Add amazing feature'`
5. Push: `git push origin feature/amazing`
6. Open a Pull Request

## 📄 License

GlowLang is released under the **GlowLang Sovereign License (GSL) v1.0** — see [GlowLang Sovereign License](GlowLang%20Soverign%20License.md) for the full terms. All rights reserved by the GlowLang maintainers.

---

<p align="center">
  Built with ❤️ by <a href="https://github.com/AnshuMishra1122003">Anshumali</a>
</p>
