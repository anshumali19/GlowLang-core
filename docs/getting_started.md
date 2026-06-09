# Getting Started with GlowLang

Welcome to **GlowLang** — a production-grade programming language with natural-language readability and native C-tier performance. This guide will have you writing and running GlowLang programs in minutes.

---

## Prerequisites

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
git clone https://github.com/user/GlowLang.git
cd GlowLang
make
```

This produces the GlowLang binary at `./bin/glowlang`.

Verify your installation:
```bash
./bin/glowlang --version
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
