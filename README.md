<p align="center">
  <h1 align="center">🔥 GlowLang</h1>
  <p align="center"><strong>A programming language with natural-language syntax, a register bytecode VM with an x86-64 JIT, SIMD tensors, built-in HTTP and SQLite, and a growing standard library and tooling.</strong></p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C-blue" alt="Language">
  <img src="https://img.shields.io/badge/tests-129%20(output--checked)-brightgreen" alt="Tests">
  <img src="https://img.shields.io/badge/license-Sovereign-blue" alt="License">
  <img src="https://img.shields.io/badge/platform-Windows%20x64%20(verified)%20%7C%20Linux%20%7C%20macOS-lightgrey" alt="Platform">
</p>

---

## ✨ Features

| Category | What exists today |
|----------|-------------------|
| **Natural syntax** | `show`, `when`, `whenever`, `every`, `blueprint`, `action`/`can`, `use`/`import` |
| **Runtime** | Register-based bytecode VM; x86-64 template JIT (Windows x64 and System V ABIs) with on-stack replacement for hot loops; non-moving mark-and-sweep GC |
| **Numbers** | 48-bit inline integers that promote to exact doubles and then big integers instead of overflowing; exact integer `**` |
| **Concurrency** | `spawn` runs functions on OS threads under a global interpreter lock (one thread runs GlowLang code at a time); `threads.join_all()` |
| **HTTP** | Built-in TCP/HTTP server (`http.listen`, `http.accept`, `http.read`, `http.send`) and client |
| **Database** | SQLite bindings (`sqlite.open`, `sqlite.exec`, `sqlite.query`, `sqlite.close`) |
| **Tensors** | `tensor.zeros/ones/add/dot/matmul`; AVX2+FMA kernels chosen at run time when the CPU has them, scalar code otherwise |
| **Security** | OpenSSL SHA-256 and HMAC (`crypto.sha256`, `crypto.hmac`); list indexing is bounds-checked (an out-of-range index gives `none`) |
| **Text** | Built-in regular expressions (same engine on every OS), JSON (`json.parse`, `json.stringify`) |
| **Filesystem** | `fs.read`, `fs.write`, `fs.exists`, `fs.mkdir`, `fs.remove` |
| **Tooling** | `trace`, `debug`, `check` and `package` subcommands, profiler, LSP server, `gdu` package manager |
| **Editors** | VS Code, Neovim, Sublime Text, Helix, JetBrains |
| **Testing** | 129 test programs whose output is compared line by line with `tests/expected/`, fuzz harnesses, ASan/TSan make targets |

## ⚡ Performance

Measured with `benchmarks/run-benchmarks.ps1` on Windows (Intel i7-14650HX, MSVC build, Python 3.13.15). Times are whole-process wall time, median of 7 runs, and both languages must print the same answer. The full table and the method are in [benchmarks/RESULTS.md](benchmarks/RESULTS.md).

| Benchmark | GlowLang | CPython | CPython time / GlowLang time |
|---|---:|---:|---:|
| Fibonacci (sum fib 0..29) | 57 ms | 128 ms | 2.24x |
| Loop (1M additions) | 17 ms | 90 ms | 5.18x |
| Ackermann(3, 8) | 64 ms | 190 ms | 2.98x |
| N-Queens (N=10, objects) | 41 ms | 98 ms | 2.42x |
| Dijkstra (1000 nodes) | 77 ms | 174 ms | 2.26x |
| Lists + dict (1M push, 200k keys) | 110 ms | 253 ms | 2.31x |
| Strings (200k concat + split) | 48 ms | 72 ms | 1.48x |

GlowLang is faster than CPython on these workloads, and far slower than compiled languages such as C, Go or Rust.

## 🚀 Quick Start

### Windows (x64)

Requirements: Visual Studio 2022+ Build Tools (C++), `win_flex`/`win_bison` (`winget install WinFlexBison.win_flex_bison`), and vcpkg with `openssl:x64-windows-static sqlite3:x64-windows-static`.

```powershell
.\build.bat                      # builds and signs bin\glowlang.exe (see scripts\build-windows.ps1)
.\bin\glowlang.exe run examples\hello.glow
.\run_all_tests.bat              # 129 tests, output-checked
```

`build.bat -NoSign` skips signing. The build signs with a self-signed development certificate; Windows does not trust that certificate on other machines, and Smart App Control (when enabled) may block a fresh build. In that case rebuild, or sign with a trusted code-signing certificate.

### Linux / macOS

```bash
# Ubuntu/Debian dependencies
sudo apt-get install gcc make flex bison libsqlite3-dev libssl-dev libx11-dev

make                  # bin/glowlang and bin/gdu
./bin/glowlang run examples/hello.glow
make test             # output-checked test suite (scripts/run-tests.sh)
```

The Linux and macOS builds are set up in the `Makefile`, `CMakeLists.txt` and CI, but the results on this page were measured on Windows only.

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
sqlite.close(db)
```

### SIMD Tensors
```
let a = tensor.ones(1000000)
let b = tensor.ones(1000000)
let c = tensor.add(a, b)        # AVX2 when the CPU supports it
let dot = tensor.dot(a, b)      # AVX2 + FMA dot product
```

### Concurrency
```
func worker(id):
    show "Worker " + id + " running"
end

spawn worker(1)
spawn worker(2)
threads.join_all()              # the program also waits for its threads at exit
```

Threads share one interpreter lock, so `spawn` is for overlapping waiting (sleep, sockets, HTTP), not for running GlowLang code on several cores at once. See the [Concurrency Guide](docs/concurrency_guide.md).

## 📦 Project Structure

```
GlowLang/
├── src/                    # Core runtime (C)
│   ├── glowlang.y          # Parser (Bison)
│   ├── glowlang.l          # Lexer (Flex)
│   ├── compiler.c          # Bytecode compiler
│   ├── opt.c               # Bytecode optimizer
│   ├── vm.c                # Virtual machine, threads, GIL
│   ├── jit.c               # x86-64 JIT compiler
│   ├── gc.c                # Garbage collector
│   ├── stdlib.c            # Native standard library
│   ├── regex.c             # Regular expression engine
│   └── lsp.c               # Language Server Protocol
├── lib/                    # Libraries written in GlowLang
├── tests/                  # Test programs; expected output in tests/expected/
├── benchmarks/             # Benchmark programs, runner and RESULTS.md
├── scripts/                # Windows build/sign scripts and test runners
├── examples/               # Example applications
├── editors/                # Editor integrations
├── tools/                  # Dev tools (gdu, debugger, profiler, inspector)
├── docs/                   # Documentation
└── Makefile / CMakeLists.txt
```

## 🧪 Testing

Every `tests/*.glow` program is run with a timeout, and the runner checks its exit code, compares its stdout line by line with `tests/expected/<name>.out`, and requires listed lines in its stderr.

```bash
make test                                   # Linux/macOS
GLOW_NOJITEXEC=1 ./scripts/run-tests.sh     # same suite with the JIT disabled
make test-sanitized                         # ASan + TSan builds
make fuzz-all                               # fuzz harnesses
```

```powershell
.\scripts\run-tests.ps1                                   # Windows
.\scripts\run-tests.ps1 -ExtraEnv @{GLOWLANG_GC_STRESS='1'}
```

On Windows the suite passes in four modes: default, interpreter only (`GLOW_NOJITEXEC=1`), GC stress (`GLOWLANG_GC_STRESS=1`) and scalar math (`GLOW_NO_SIMD=1`).

## 📖 Documentation

- **[Getting Started](docs/getting_started.md)** — Tutorial for beginners
- **[Language Reference](docs/language_reference.md)** — Syntax reference
- **[Web Development](docs/web_guide.md)** — HTTP server and APIs
- **[AI/ML Guide](docs/ai_ml_guide.md)** — Tensors and neural networks
- **[Concurrency Guide](docs/concurrency_guide.md)** — `spawn`, the GIL, `whenever`
- **[Runtime Internals](docs/runtime_internals.md)** — VM, JIT, GC
- **[Architecture](ARCHITECTURE.md)** — Components and build system
- **[Deployment Guide](docs/deployment_guide.md)** and **[Production Deployment](docs/production_deployment.md)**

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
  Compiler → Register bytecode
    ↓
  Optimizer → Optimized bytecode
    ↓
  Interpreter ⇄ JIT (functions after 5 calls, loops after 1000 iterations)
    ↓
  GC (non-moving mark-and-sweep)
```

## 🤝 Contributing

1. Fork the repository
2. Create your feature branch: `git checkout -b feature/amazing`
3. Run the tests: `make test` (or `.\run_all_tests.bat` on Windows)
4. Commit: `git commit -m 'Add amazing feature'`
5. Push: `git push origin feature/amazing`
6. Open a Pull Request

## 📄 License

GlowLang is released under the **GlowLang Sovereign License (GSL) v1.0** — see [GlowLang Sovereign License](GlowLang%20Soverign%20License.md) for the full terms. All rights reserved by the GlowLang maintainers.

---

<p align="center">
  Built with ❤️ by <a href="https://github.com/AnshuMishra1122003">Anshumali</a>
</p>
