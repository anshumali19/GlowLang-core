# Getting Started with GlowLang

Welcome to **GlowLang** — a programming language with natural-language readability, a bytecode VM with a tiered JIT, and a batteries-included standard library. This guide will have you writing and running GlowLang programs in minutes.

---

## Install on Windows: Microsoft Store

On Windows, install GlowLang from the Microsoft Store. There is nothing to build:

- Store page: **[apps.microsoft.com/detail/9P2D8F466LBX](https://apps.microsoft.com/detail/9P2D8F466LBX)** (Store ID `9P2D8F466LBX`), or
- from a terminal:

  ```powershell
  winget install --id 9P2D8F466LBX --source msstore
  ```

The Store version is signed by Microsoft, so Windows runs it without SmartScreen or Smart App Control warnings, and the Store keeps it up to date. Afterwards `glowlang` and `gdu` work in every terminal (no PATH changes), and the Start menu has a **GlowLang** entry that opens the REPL. Check it:

```powershell
gdu --version
```

Then skip ahead to [Your First Program](#your-first-program).

## Install on Linux

```bash
mkdir -p ~/glowlang
curl -fsSL https://github.com/anshumali19/GlowLang-packages/releases/latest/download/glowlang-linux-x64.tar.gz | tar -xz -C ~/glowlang
echo 'export PATH="$HOME/glowlang:$PATH"' >> ~/.bashrc && export PATH="$HOME/glowlang:$PATH"
glowlang --version
```

The Linux build runs on x64 with glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+) and needs libX11 and SQLite, which most systems have (`sudo apt-get install libx11-6 libsqlite3-0` if not).

## Install on macOS

```bash
arch=$([ "$(uname -m)" = arm64 ] && echo arm64 || echo x64)
mkdir -p ~/glowlang
curl -fsSL https://github.com/anshumali19/GlowLang-packages/releases/latest/download/glowlang-macos-$arch.tar.gz | tar -xz -C ~/glowlang
echo 'export PATH="$HOME/glowlang:$PATH"' >> ~/.zshrc && export PATH="$HOME/glowlang:$PATH"
glowlang --version
```

This picks the Apple silicon or Intel build. Each archive holds `glowlang`, `gdu` and the standard libraries in `lib/`; keep `lib/` next to `glowlang`. All downloads, with SHA-256 checksums, are on the [releases page](https://github.com/anshumali19/GlowLang-packages/releases/latest). To work on GlowLang itself, build from source as described below.

---

## Prerequisites (building from source)

Ensure your system has the following installed:

| Tool | Version | Purpose |
|------|---------|---------|
| `gcc` | 9+ | C compiler for building GlowLang |
| `flex` | 2.6+ | Lexer generator |
| `bison` | 3.5+ | Parser generator |
| `libssl-dev` | 1.1+ | OpenSSL bindings (crypto, HTTPS) |
| `libsqlite3-dev` | 3.30+ | SQLite database support |
| `make` | 4.0+ | Build automation |

### Install on Ubuntu/Debian
```bash
sudo apt update
sudo apt install gcc flex bison libssl-dev libsqlite3-dev make
```

### Install on Fedora/RHEL
```bash
sudo dnf install gcc flex bison openssl-devel sqlite-devel make
```

### Install on macOS
```bash
brew install flex bison openssl sqlite
```

---

## Building GlowLang

Clone the repository and build from source:

```bash
git clone https://github.com/anshumali19/GlowLang-core.git
cd GlowLang-core
make
```

This produces the GlowLang binary at `./bin/glowlang`.

Verify your installation:
```bash
./bin/glowlang run examples/hello.glow
```

---

## Your First Program

Create a file called `hello.glow`:

```glow
# hello.glow — Your first GlowLang program

let name = "World"
show f"Hello, {name}!"

# Define a function
func greet(person):
    return "Welcome to GlowLang, " + person + "!"
end

show greet("Developer")
```

Run it:
```bash
./bin/glowlang hello.glow
```

**Output:**
```
Hello, World!
Welcome to GlowLang, Developer!
```

---

## Core Concepts at a Glance

### Variables
```glow
let x = 42              # Integer
let pi = 3.14159        # Float
let name = "GlowLang"   # String
let active = true       # Boolean
let items = [1, 2, 3]   # List
let config = {"key": "value"}  # Dictionary
```

### Output
```glow
show "Hello, World!"
show "Value:", x
show f"Pi is approximately {pi}"
```

### Functions
GlowLang supports multiple function declaration keywords — `func`, `action`, and `can` — all behave identically:

```glow
func add(a, b):
    return a + b
end

action multiply(a, b):
    return a * b
end

show add(3, 4)        # 7
show multiply(3, 4)   # 12
```

### Control Flow
GlowLang offers two styles of conditionals:

```glow
# Traditional style
if x > 10:
    show "Large"
else:
    show "Small"
end

# Natural-language style
when x > 10:
    show "Large"
eif x > 5:
    show "Medium"
otherwise:
    show "Small"
end
```

### Loops
```glow
# While loop
let i = 0
while i < 5:
    show i
    i = i + 1
end

# For-each loop
let fruits = ["apple", "banana", "cherry"]
for each fruit in fruits:
    show fruit
end

# Range loop
every i from 0 to 9:
    show i
end
```

### Classes and Objects
```glow
class Animal:
    func init(name, sound):
        self.name = name
        self.sound = sound
    end

    func speak():
        show f"{self.name} says {self.sound}!"
    end
end

let dog = Animal("Rex", "Woof")
dog.speak()   # Rex says Woof!
```

### Error Handling
```glow
try:
    let result = 10 / 0
catch e:
    show "Error caught:", e
end
```

---

## Interactive REPL

Launch the REPL for interactive exploration:

```bash
./bin/glowlang
```

Type GlowLang expressions directly and see results instantly. Perfect for learning and prototyping.

```
glow> let xs = [3, 1, 2]
glow> sorted(xs)
[1, 2, 3]
glow> func double(x):
....      return x * 2
....  end
glow> double(21)
42
```

- An expression on its own shows its value (unless it is `none`).
- A block (`func`, `when`, `for`, `class`, ...) continues on `....` lines until its `end`, and so does a line that leaves a bracket open.
- To give up on an unfinished block, press Enter on two empty lines, or Ctrl+C.
- Up and Down bring back earlier lines, also from earlier sessions (they are kept in
  `~/.glowlang_history` on Linux and macOS; the Windows console keeps its own). Left, Right,
  Home, End, Backspace and Delete edit the line; Ctrl+A and Ctrl+E jump to its start and end,
  Ctrl+U and Ctrl+K delete to them, Ctrl+L clears the screen, Tab puts in four spaces, and
  Ctrl+D on an empty line quits.
- An error is reported and the session carries on. Type `exit` to quit.
- `glowlang repl < session.glow` runs a file as if it were typed in, without the prompts.

---

## Starting a Project

```bash
glowlang new myapi            # a web API: sign-up, login, SQLite, tests
glowlang new mytool --cli     # or a command-line tool
cd myapi
glowlang test                 # runs the test_*.glow files
glowlang watch app.glow       # runs the app again on every save
glowlang fmt                  # re-indents every .glow file here
glowlang package app.glow     # one executable to copy anywhere
```

See [Testing your code](testing_your_code.md) and
[Shipping a script as one file](shell_scripting.md#shipping-a-script-as-one-file).

## Standard Modules

`import` a module by name to use it:

```glow
import "toml"
import "log"
let config = toml.parse("[server]\nport = 8080\n")
log.info("starting", {"port": config["server"]["port"]})
```

Among them: `csv`, `toml`, `yaml`, `ini` and `dotenv` for files and
settings; `template`, `markdown`, `schema`, `url` and `retry` for web apps;
`cron` and `datetime` for times; `textwrap` and `humanize` for text; `log`,
`lru`, `collections`, `semver`, `diff` and `re`. Each is described in
the [language reference](language_reference.md#18-standard-library).

---

## Running Examples

GlowLang ships with a rich set of examples:

```bash
# Web server
./bin/glowlang examples/web_api.glow

# AI/Tensor computation
./bin/glowlang examples/ai_demo.glow

# Async concurrency
./bin/glowlang examples/async_system.glow

# Database operations
./bin/glowlang examples/database_crud.glow
```

---

## Project Structure

A typical GlowLang project looks like this:

```
my_project/
├── Glow.deps          # Dependencies file
├── Glow.lock          # Lock file for reproducible builds
├── src/
│   ├── main.glow      # Entry point
│   └── utils.glow     # Utility modules
├── lib/
│   └── helpers.glow   # Library code
└── tests/
    └── test_main.glow # Tests
```

### Importing Modules
```glow
# Import a local file
import "lib/helpers.glow"

# Import a remote package (via gdu (Glow Dependency Utility))
import "https://github.com/user/math.glow"
```

---

## Package Management with gdu (Glow Dependency Utility)

GlowLang's built-in package manager `gdu (Glow Dependency Utility)` handles dependencies:

```bash
# Add a dependency
gdu add https://github.com/user/package.glow

# Install all dependencies from Glow.deps
gdu install

# List installed packages
gdu list
```

Dependencies are declared in `Glow.deps` and locked in `Glow.lock` for reproducible builds.

---

## What's Next?

| Guide | Description |
|-------|-------------|
| [Language Reference](language_reference.md) | Complete syntax and semantics |
| [Async Guide](async_guide.md) | Concurrency with `spawn` and tasks |
| [AI/ML Guide](ai_ml_guide.md) | Tensor operations and neural networks |
| [Web Guide](web_guide.md) | Building web servers and APIs |
| [Runtime Internals](runtime_internals.md) | VM, JIT, and GC architecture |
| [Optimization Guide](optimization_guide.md) | Performance tips and best practices |
| [Deployment Guide](deployment_guide.md) | Packaging and deploying GlowLang apps |

---

*GlowLang: Natural Language, Native Speed.*
