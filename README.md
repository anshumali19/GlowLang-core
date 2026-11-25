<p align="center">
  <h1 align="center">🔥 GlowLang</h1>
  <p align="center"><strong>A fast, free programming language that reads like plain English: a JIT-compiled VM, built-in web server, PostgreSQL, SQLite, WebSockets and SIMD tensors, on Windows, Linux and macOS.</strong></p>
</p>

<p align="center">
  <a href="https://github.com/anshumali19/GlowLang-packages/releases/latest"><img src="https://img.shields.io/github/v/release/anshumali19/GlowLang-packages?label=release" alt="Latest release"></a>
  <a href="https://apps.microsoft.com/detail/9P2D8F466LBX"><img src="https://img.shields.io/badge/Microsoft%20Store-Get%20GlowLang-0078D4" alt="Get GlowLang from the Microsoft Store"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey" alt="Platform">
  <img src="https://img.shields.io/badge/tests-output--checked%20on%203%20OSes-brightgreen" alt="Tests">
  <img src="https://img.shields.io/badge/license-Apache%202.0%20%2B%20Commons%20Clause-blue" alt="License">
</p>

---

**GlowLang** is a general-purpose programming language with a syntax close to Python and English
(`show`, `when` / `otherwise`, `every`, `blueprint`), written in C. Programs run on a register
bytecode virtual machine with JIT compilers for x86-64 and ARM64 and a generational garbage
collector, and on the benchmarks below they finish 1.4× to 6.7× faster than CPython.
It ships with what web and data programs need: an HTTP server and client, PostgreSQL and SQLite,
WebSockets, JSON, JWT / OAuth logins, SIMD tensors and its own package manager, `gdu`.

The current version is **1.13.0** ([what's new](docs/release_notes_1.13.0.md): thirteen standard
modules written in GlowLang, among them YAML, TOML, CSV, templates, Markdown, JSON Schema, URLs and
logging; GlowWeb keep-alive, cookies and `PATCH` routes; and regular expressions compiled once).

## 📥 Install

### Windows

Install GlowLang from the **[Microsoft Store](https://apps.microsoft.com/detail/9P2D8F466LBX)**
(Store ID `9P2D8F466LBX`), or from a terminal:

```powershell
winget install --id 9P2D8F466LBX --source msstore
```

The Store version is signed by Microsoft, so SmartScreen and Smart App Control run it without
warnings, and it updates itself. `glowlang` and `gdu` then work in every terminal without
editing PATH, and the Start menu gets a GlowLang entry that opens the REPL.

### Linux (x64)

```bash
mkdir -p ~/glowlang
curl -fsSL https://github.com/anshumali19/GlowLang-packages/releases/latest/download/glowlang-linux-x64.tar.gz | tar -xz -C ~/glowlang
echo 'export PATH="$HOME/glowlang:$PATH"' >> ~/.bashrc && export PATH="$HOME/glowlang:$PATH"
glowlang --version
```

It needs glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+) plus libX11 and SQLite,
which most systems already have (`sudo apt-get install libx11-6 libsqlite3-0` if not).

### macOS (Apple silicon and Intel)

```bash
arch=$([ "$(uname -m)" = arm64 ] && echo arm64 || echo x64)
mkdir -p ~/glowlang
curl -fsSL https://github.com/anshumali19/GlowLang-packages/releases/latest/download/glowlang-macos-$arch.tar.gz | tar -xz -C ~/glowlang
echo 'export PATH="$HOME/glowlang:$PATH"' >> ~/.zshrc && export PATH="$HOME/glowlang:$PATH"
glowlang --version
```

Every download is listed, with SHA-256 checksums, on the
[releases page](https://github.com/anshumali19/GlowLang-packages/releases/latest). Keep the
`lib/` folder next to `glowlang`: it holds the standard libraries. To update later, run
`gdu install glowlang`: it installs the latest release in `~/.glowlang/bin` and adds that folder
to your PATH in `~/.bashrc` and `~/.zshrc`.

## 🚀 Your first program

Save this as `hello.glow`:

```glow
show "Hello, World!"

let name = "GlowLang"
every n in [1, 2, 3]:
    show f"{name} counts {n}"
end
```

Run it:

```bash
glowlang run hello.glow
```

`glowlang` with no arguments opens the REPL. `glowlang check app.glow` finds syntax errors
without running anything, and `gdu install <package>` adds a package to the project.

| Command | What it does |
|---|---|
| `glowlang new myapi` | A working web API: sign-up, login, a database and tests (`--cli` for a command-line tool) |
| `glowlang watch app.glow` | Runs it, and again each time you save a `.glow` file |
| `glowlang test` | Runs the `test_` functions in every `test_*.glow` file ([guide](docs/testing_your_code.md)) |
| `glowlang fmt` | Re-indents every `.glow` file the same way (`--check` for CI) |
| `glowlang package app.glow` | One executable to copy anywhere; `--target linux-x64` and friends build for another system |
| `glowlang profile app.glow` | Runs it, then lists the lines that ran most |

## 🏗️ The language at a glance

```glow
# Variables, dicts and f-strings
let scores = {"asha": 91, "ravi": 78, "meera": 85}

# Loops and conditions read like English
every student, score in scores:
    when score >= 85:
        show f"{student}: excellent"
    otherwise:
        show f"{student}: keep going"
    end
end

# Functions with default and named arguments
func greet(person, greeting = "Hello", punctuation = "!"):
    return greeting + ", " + person + punctuation
end
show greet("Ravi", punctuation = "?")      # Hello, Ravi?

# Classes (blueprint is another word for class; self is implicit)
class Account:
    func init(owner, balance = 0):
        self.owner = owner
        self.balance = balance
    end
    func deposit(amount):
        self.balance = self.balance + amount
        return self.balance
    end
end
let acc = Account("Meera")
acc.deposit(500)

# match / case, enums and sets
enum Size: SMALL, MEDIUM, LARGE
func describe(value):
    match value:
        case 0:
            return "zero"
        case [first, ...rest]:
            return f"a list starting with {first}"
        otherwise:
            return "something else"
    end
end
show describe([4, 5, 6]), Size.LARGE      # a list starting with 4 Size.LARGE
show {1, 2, 2, 3} | {3, 4}                # {1, 2, 3, 4}

# Generators: values made one at a time, as they are asked for
func count_up(limit):
    let i = 0
    while i < limit:
        yield i
        i += 1
    end
end
show [n * n for each n in count_up(4)]   # [0, 1, 4, 9]

# with: the resource is let go of however the block ends
with sqlite.open(":memory:") as db:
    sqlite.exec(db, "CREATE TABLE notes (text TEXT)")
end                                       # db is closed here

# Decorators, and **opts to pass on every argument
import "functools"
@cache
func fib(n):
    when n < 2:
        return n
    end
    return fib(n - 1) + fib(n - 2)
end
show fib(90)                              # 2880067194370816120, at once
func tag(name, **attrs):
    return name + " " + str(attrs)
end
show tag("p", id = "intro")               # p {"id": "intro"}

# Errors, none-safe access and floor division
try:
    let user = none
    show user?.name ?? "guest"            # guest
    show 10 // 3, -7 // 2                 # 3 -4
    throw "something went wrong"
catch e:
    show "caught:", e
finally:
    show "done"
end
```

Comments start with `#` (or `/* ... */`); `//` is floor division, as in Python.
[GlowLang in 10 minutes](docs/glowlang_in_10_minutes.md) walks through the rest, and the
[language reference](docs/language_reference.md) covers everything.

### A web server

```glow
let server = http.listen(8080)
show "Server on http://localhost:8080"

while true:
    let client = http.accept(server)
    let req = http.read(client)
    http.send(client, "HTTP/1.1 200 OK\r\n\r\nHello from GlowLang!")
    http.close(client)
end
```

For routes, JSON APIs and middleware there is the GlowWeb framework: see the
[web guide](docs/web_guide.md) and the [GlowWeb tutorial](docs/glowweb_tutorial.md).

`glowlang new myapi` starts a whole API in one step: sign-up, login with tokens,
hashed passwords, a SQLite database and tests that call every endpoint. Nothing
else needs installing:

```bash
glowlang new myapi
cd myapi
glowlang run test_app.glow    # 16 checks
glowlang run app.glow         # http://localhost:8000
glowlang watch app.glow       # restarts it each time you save a .glow file
```

### A database

```glow
let db = sqlite.open("app.db")
sqlite.exec(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
sqlite.exec(db, "INSERT INTO users (name, age) VALUES ('Alice', 30)")
for each row in sqlite.query(db, "SELECT * FROM users"):
    show row["name"] + " is " + row["age"]
end
sqlite.close(db)
```

PostgreSQL is built in too: see [postgres.md](docs/postgres.md).

## ⚡ Performance

GlowLang against CPython on the same programs, measured with
[`benchmarks/run-benchmarks.sh`](benchmarks/run-benchmarks.sh): whole-process wall time
(startup included), median of 7 runs, and both languages must print the same answer.
GlowLang 1.13.0 and Python 3.14.4 on Linux (WSL 2), Intel Core i7-14650HX.

| Benchmark | GlowLang 1.13.0 | CPython 3.14 | GlowLang is faster by |
|---|---:|---:|---:|
| Fibonacci (sum fib 0..29) | 32 ms | 85 ms | 2.67× |
| Loop (1M additions) | 9 ms | 60 ms | 6.73× |
| Ackermann(3, 8) | 39 ms | 137 ms | 3.52× |
| N-Queens (N=10, objects) | 26 ms | 60 ms | 2.33× |
| Dijkstra (1000 nodes) | 41 ms | 116 ms | 2.81× |
| Lists + dict (1M push, 200k keys) | 69 ms | 181 ms | 2.64× |
| Strings (200k concat + split) | 35 ms | 48 ms | 1.38× |

The report with the machine details is [benchmarks/RESULTS-linux.md](benchmarks/RESULTS-linux.md);
[benchmarks/RESULTS.md](benchmarks/RESULTS.md) has an older run on Windows
(`benchmarks/run-benchmarks.ps1`). GlowLang is far slower than compiled languages such as C, Go
or Rust.

In 1.13.0, GlowWeb keeps connections open (21,988 requests a second from 8 clients that reuse
their connection), patterns are compiled once (`re.find` 52% faster), and the new modules are
timed against Python in [benchmarks/libs](benchmarks/libs/README.md): `yaml` parses 6.6× as fast as
PyYAML's Python parser, `schema` checks 2× to 3.6× as fast as `jsonschema`, `diff` is 7× as fast
as `difflib`, while `url`, `semver` and `toml` are slower than their Python counterparts.
In 1.12.0, a web server under concurrent load answers up to 22× more requests a second:
GlowWeb serves 9,324 requests a second to 8 clients, where 1.11.0 served 412, Python's
`http.server` 3,532 and Node.js 16,588 ([benchmarks/web](benchmarks/web/README.md)).
In 1.11.0, `name = value` calls to methods are 3× faster and to constructors 2× faster, a function
that uses `show` or `throw` is compiled (calls to it about 4× faster), and `parallel.map` spreads
work over every core: 7.8× faster on 24 threads ([release notes](docs/release_notes_1.11.0.md)).
In 1.10.0, a call with `name = value` arguments to a function defined in the program runs as fast
as a plain call, 3× faster than in 1.9.0 ([release notes](docs/release_notes_1.10.0.md)). 1.9.0
made `name = value` and `...spread` calls 23 to 43% faster than 1.8.0
([1.9.0 release notes](docs/release_notes_1.9.0.md)). 1.8.0 made binary_trees 21% faster and cut its
memory from 138 MB to 76 MB compared with 1.7.0
([1.8.0 release notes](docs/release_notes_1.8.0.md)).

## ✨ Features

| Category | What exists today |
|----------|-------------------|
| **Syntax** | `show`, `when` / `eif` / `otherwise`, `every`, `for each key, value in`, `is in`, `blueprint` / `class`, `action` / `func`, `match` / `case`, `enum`, `try` / `catch` / `finally`, `with` blocks, generators (`yield`, `yield from`, `next`), decorators (`@cache`), unpacking (`let a, b = b, a`), named arguments, `...rest` / `**opts` and `...spread` / `**dict`, `??` and `?.`, list / dict / set comprehensions |
| **Runtime** | Register bytecode VM; JIT compilers for x86-64 (Windows and System V) and ARM64 (Apple silicon, ARM Linux) with on-stack replacement for hot loops; generational, non-moving garbage collector |
| **Numbers** | Integers of any size (never overflow), floats, exact `**`, Python-style `//` and `%`, bitwise `& \| ^ ~ << >>`, literals like `0xff`, `0b1010` and `1_000` |
| **Collections** | Lists, dicts and sets compared by contents; slices with steps; `sort`, `map`, `filter`, `reduce`, `sorted`, `min`, `max`, `sum`, `enumerate`, `zip`; a heap, a deque and counting (`collections`); a size-limited cache with expiry (`lru`) |
| **Web** | HTTP server and HTTPS client (`http.request`: certificate checks, redirects, timeouts), the GlowWeb framework, WebSocket client and server (RFC 6455); URLs (`url`), Mustache templates (`template`), Markdown to HTML (`markdown`), JSON Schema checks (`schema`) |
| **Databases** | SQLite built in; a PostgreSQL client with TLS, SCRAM logins, parameterized queries and transactions; Redis |
| **Security** | SHA-256, HMAC, BLAKE3, random tokens; JWT signing and checking (HS / RS / ES); OAuth 2.0 and OpenID Connect sign-in with Google, Microsoft and GitHub presets |
| **Services** | Clients for Razorpay, WhatsApp Cloud API, SMS through Twilio and Google Sheets |
| **Data and AI** | SIMD tensors (AVX2 + FMA when the CPU has them); `glownum`, `glowframe`, `glowlearn`, `glowgrad` and `glowtext`, libraries like NumPy, pandas, scikit-learn and PyTorch written in GlowLang |
| **Concurrency** | `spawn` on OS threads with a global interpreter lock; `parallel.map` over worker processes for every core; channels, tasks and timers in `lib/async.glow` |
| **Text** | Unicode strings (lengths and slices count characters), f-strings, regular expressions, JSON, CSV with quoted cells, TOML, YAML and INI config files, line diffs in `diff -u` form (`diff`) |
| **Tooling** | `check`, `trace`, `debug`, `profile` and `package` subcommands; a language server (errors, completion, hover, go-to-definition); the `gdu` package manager |
| **Apps** | Log lines with levels and fields, as text or JSON (`log`); settings from `.env` files (`dotenv`); semantic versions and npm-style ranges (`semver`); cron schedules and their next run (`cron`); calls retried with growing waits (`retry`) |
| **Editors** | VS Code, Neovim, Sublime Text, Helix, JetBrains |

## 📖 Documentation

**Start here**
- [Getting Started](docs/getting_started.md): install, first program, packages
- [GlowLang in 10 Minutes](docs/glowlang_in_10_minutes.md): the language by example ([examples/tour.glow](examples/tour.glow))
- [Tools you can ship as one file](examples/tools/README.md): a log summarizer, CSV cleaner, folder backup, API checker and bulk renamer, each a single executable with `glowlang package`
- [Language Reference](docs/language_reference.md): every statement, operator and built-in
- [Release notes 1.13.0](docs/release_notes_1.13.0.md) (and [1.12.0](docs/release_notes_1.12.0.md), [1.11.0](docs/release_notes_1.11.0.md), [1.10.0](docs/release_notes_1.10.0.md), [1.9.0](docs/release_notes_1.9.0.md), [1.8.0](docs/release_notes_1.8.0.md))

**Building things**
- Web: [web guide](docs/web_guide.md), [GlowWeb tutorial](docs/glowweb_tutorial.md), [WebSockets](docs/websocket.md)
- Data: [PostgreSQL](docs/postgres.md), [AI libraries](docs/ai_libraries.md), [AI/ML guide](docs/ai_ml_guide.md), [tensors](docs/tensor_guide.md)
- Logins and services: [JWT and OAuth](docs/jwt_oauth.md), [Razorpay, WhatsApp, SMS, Google Sheets](docs/services.md)
- Programs and scripts: [concurrency](docs/concurrency_guide.md), [async](docs/async_guide.md), [processes](docs/process_guide.md), [shell scripting](docs/shell_scripting.md), [FFI](docs/ffi_guide.md)
- Packages: [writing a package](docs/package_authoring.md), [the ecosystem](docs/ecosystem_guide.md)
- Standard modules (CSV, TOML, YAML, `.env`, templates, Markdown, JSON Schema, URLs, logging, caches, diffs, ...): [the reference](docs/language_reference.md#18-standard-library), and their speed against Python in [benchmarks/libs](benchmarks/libs/README.md)

**Running and shipping**
- [Testing your code with `glowlang test`](docs/testing_your_code.md), [debugger](docs/debugger_guide.md), [profiler](docs/profiler_guide.md), [optimization](docs/optimization_guide.md)
- [Deployment](docs/deployment_guide.md), [production deployment](docs/production_deployment.md), [CI/CD](docs/cicd_setup.md)

**Inside GlowLang**
- [Architecture](ARCHITECTURE.md), [runtime internals](docs/runtime_internals.md) (VM, JIT, GC), [fuzzing](docs/fuzzing.md)

More guides and tutorials: [GlowLang-docs](https://github.com/anshumali19/GlowLang-docs).
Complete example programs: [GlowLang-examples](https://github.com/anshumali19/GlowLang-examples).

## ❓ FAQ

**How do I install GlowLang?**
On Windows, from the [Microsoft Store](https://apps.microsoft.com/detail/9P2D8F466LBX) or with
`winget install --id 9P2D8F466LBX --source msstore`. On Linux and macOS, with the `curl` command
under [Install](#-install).

**How do I run a `.glow` file?**
`glowlang run file.glow`. With no file, `glowlang` opens an interactive REPL.

**Is GlowLang faster than Python?**
On the benchmarks above it finishes 1.4× to 6.7× sooner than CPython 3.14, startup included. It
is not as fast as compiled languages like C, Go or Rust.

**Is GlowLang like Python?**
Close. Blocks end with `end` instead of depending on indentation, and keywords such as `show`,
`when`, `every` and `blueprint` read like English, while `if`, `else`, `for each`, `class` and
`print` work too.
`//`, `%`, slices, f-strings, comprehensions and named arguments follow Python's rules,
generators work as Python's do, and
`...rest` and `**opts` do what `*args` and `**kwargs` do, and decorators work as Python's do.

**How do I install packages?**
`gdu install <name>` inside your project, then `use "<name>"` in the code. See
[writing a package](docs/package_authoring.md) to publish your own.

**Does it run on Apple silicon and ARM Linux?**
Yes. Releases are built and tested for macOS on Apple silicon and Intel, Linux x64 and Windows
x64, and the JIT also compiles for ARM64 Linux when built from source.

**Is it free for commercial use?**
Yes. Programs you write in GlowLang are yours to sell or license. GlowLang itself may not be
sold (see [License](#-license)).

**Where do I report a bug or ask a question?**
Open an [issue](https://github.com/anshumali19/GlowLang-core/issues).

## 🔨 Build from source

Needed only to work on GlowLang itself.

### Linux / macOS

```bash
# Ubuntu/Debian dependencies (macOS: brew install flex bison openssl@3 sqlite)
sudo apt-get install gcc make flex bison libsqlite3-dev libssl-dev libx11-dev

make                  # bin/glowlang and bin/gdu
./bin/glowlang run examples/hello.glow
make test             # output-checked test suite (scripts/run-tests.sh)
```

### Windows (x64)

Requirements: Visual Studio 2022+ Build Tools (C++), `win_flex` / `win_bison`
(`winget install WinFlexBison.win_flex_bison`), and vcpkg with
`openssl:x64-windows-static sqlite3:x64-windows-static`.

```powershell
.\build.bat                      # builds and signs bin\glowlang.exe (see scripts\build-windows.ps1)
.\bin\glowlang.exe run examples\hello.glow
.\run_all_tests.bat              # the output-checked test suite
```

`build.bat -NoSign` skips signing. The build signs with a self-signed development certificate,
which other machines do not trust, so Smart App Control (when enabled) may block a fresh build.

## 🧪 Testing

Every `tests/*.glow` program runs with a timeout; the runner checks its exit code, compares its
output line by line with `tests/expected/<name>.out`, and checks listed lines in its stderr.

```bash
make test                                   # Linux/macOS
GLOW_NOJITEXEC=1 ./scripts/run-tests.sh     # the same suite with the JIT off
GLOWLANG_GC_STRESS=1 ./scripts/run-tests.sh # collect garbage constantly
make test-sanitized                         # ASan + TSan builds
make fuzz-all                               # fuzz harnesses
```

```powershell
.\scripts\run-tests.ps1                     # Windows
```

CI builds and tests every change on Linux, macOS and Windows, plus interpreter-only, GC-stress
and AddressSanitizer runs on Linux and PostgreSQL and WebSocket integration tests. Each release
is built and tested on all four release platforms, and the published downloads are tested again
after release.

## 📦 Project structure

```
GlowLang/
├── src/                    # Runtime (C)
│   ├── glowlang.y          # Parser (Bison)
│   ├── glowlang.l          # Lexer (Flex)
│   ├── compiler.c          # Bytecode compiler
│   ├── opt.c               # Bytecode optimizer
│   ├── vm.c                # Virtual machine, threads, GIL
│   ├── jit.c               # JIT compiler (x86-64 and ARM64)
│   ├── gc.c                # Generational garbage collector
│   ├── stdlib.c            # Native standard library
│   ├── regex.c             # Regular expressions
│   └── lsp.c               # Language server
├── lib/                    # Libraries written in GlowLang
├── tests/                  # Test programs; expected output in tests/expected/
├── benchmarks/             # Benchmark programs, runners and results
├── examples/               # Example programs
├── editors/                # Editor integrations
├── tools/                  # gdu, debugger, profiler, inspector
├── docs/                   # Documentation
└── Makefile / CMakeLists.txt
```

```
Source (.glow) → Lexer → Parser → AST → Compiler → register bytecode → Optimizer
    → Interpreter ⇄ JIT (functions after 5 calls, loops after 1000 iterations)
    → generational GC
```

## 🤝 Contributing

1. Fork the repository
2. Create a branch: `git checkout -b feature/amazing`
3. Run the tests: `make test` (or `.\run_all_tests.bat` on Windows)
4. Commit, push, and open a pull request

See [CONTRIBUTING.md](CONTRIBUTING.md).

## 📄 License

GlowLang is licensed under the **Apache License 2.0 with the Commons Clause** and the GlowLang
Additional Terms; see [LICENSE](LICENSE).

- Use it for free, for anything, including commercial work. Programs you write in GlowLang are yours to sell or license as you like.
- Read, modify and fork the source, and contribute back (see [CONTRIBUTING.md](CONTRIBUTING.md)).
- GlowLang itself may not be sold or patented; it remains the work of Anshumali Sharma and Anamika Mukherjee.

---

<p align="center">
  Built with ❤️ by <a href="https://github.com/anshumali19">Anshumali</a> &amp; <a href="https://github.com/Anamika-codes">Anamika</a>
</p>
