# GlowLang — Getting Started & Deployment Guide

## What is GlowLang?

GlowLang is a production-grade programming language featuring natural-language syntax, SIMD-accelerated tensors, a built-in HTTP server, SQLite ORM, JIT compilation, and a modular package ecosystem.

---

## Prerequisites

| Dependency | Version | Purpose |
|-----------|---------|---------|
| **GCC** | 9+ | C compiler |
| **Flex** | 2.6+ | Lexer generator |
| **Bison** | 3.0+ | Parser generator |
| **libsqlite3-dev** | 3.31+ | SQLite database |
| **libssl-dev** | 1.1+ | HTTPS/TLS |
| **libfl-dev** | 2.6+ | Flex runtime |
| **libx11-dev** | any | GUI support |
| **Make** | 4.0+ | Build system |

### Install on Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y gcc flex bison libsqlite3-dev libssl-dev libfl-dev libx11-dev make
```

### Install on Fedora/RHEL
```bash
sudo dnf install gcc flex bison sqlite-devel openssl-devel libfl-devel libX11-devel make
```

### Install on macOS
```bash
brew install flex bison sqlite openssl
```

---

## Building GlowLang

```bash
git clone https://github.com/anshumali19/GlowLang.git
cd GlowLang
make all
```

This produces:
- `bin/glowlang` — The GlowLang interpreter + compiler
- `bin/gdu` — The GlowLang package manager

### Verify the build
```bash
make verify
```
Expected: `Total: 99 | Passed: 99 | Failed: 0`

---

## Running Your First Program

Create a file `hello.glow`:
```
show "Hello, World!"

let name = "GlowLang"
show "Welcome to " + name + "!"

# Natural language syntax
let items = [1, 2, 3, 4, 5]
every item in items:
    show "Item: " + item
end
```

Run it:
```bash
./bin/glowlang hello.glow
```

---

## VSCode Setup

### 1. Install the Extension

GlowLang has a built-in VSCode extension in `editors/glowlang-vscode/`:

```bash
cd editors/glowlang-vscode
# If npm is available:
npm install
npm run package
code --install-extension glowlang-*.vsix
```

Or manually install:
1. Open VSCode
2. Go to Extensions (Ctrl+Shift+X)
3. Click "..." → "Install from VSIX..."
4. Select `editors/glowlang-vscode/glowlang-*.vsix`

### 2. Manual Syntax Highlighting (Alternative)

If you can't install the extension, copy the syntax definition:

```bash
mkdir -p ~/.vscode/extensions/glowlang
cp -r editors/glowlang-vscode/* ~/.vscode/extensions/glowlang/
```

Then restart VSCode. Files ending in `.glow` will have syntax highlighting.

### 3. Configure Build Task

Create `.vscode/tasks.json` in your project:
```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Run GlowLang",
            "type": "shell",
            "command": "/path/to/GlowLang/bin/glowlang",
            "args": ["${file}"],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            },
            "problemMatcher": []
        }
    ]
}
```

Now press `Ctrl+Shift+B` to run the current `.glow` file.

### 4. Add GlowLang to PATH

For global access:
```bash
# Add to ~/.bashrc or ~/.zshrc:
export PATH="/path/to/GlowLang/bin:$PATH"
```

Then you can run from anywhere:
```bash
glowlang my_app.glow
```

---

## Other Editor Setup

### Neovim
```lua
-- In your init.lua or after/ftdetect/glowlang.lua:
vim.filetype.add({
    extension = {
        glow = "glowlang",
    },
})

-- Copy syntax config:
-- cp editors/neovim/glowlang.lua ~/.config/nvim/after/ftplugin/
```

### Sublime Text
```bash
cp editors/sublime/GlowLang.sublime-syntax ~/.config/sublime-text/Packages/User/
```

### Helix
```bash
cp editors/helix/languages.toml ~/.config/helix/
mkdir -p ~/.config/helix/runtime/queries/glowlang/
cp editors/helix/queries/glowlang/highlights.scm ~/.config/helix/runtime/queries/glowlang/
```

### JetBrains (IntelliJ, PyCharm, etc.)
1. Open Settings → Editor → File Types
2. Click "+" to add a new file type
3. Import from `editors/jetbrains/GlowLang.xml`
4. Associate `*.glow` pattern

---

## Creating a New GlowLang Project

```bash
mkdir my-project && cd my-project

# Initialize with gdu (GlowLang package manager)
/path/to/GlowLang/bin/gdu init

# This creates:
# - Glow.deps (dependency file)
# - Glow.lock (lock file)
```

### Project Structure
```
my-project/
├── Glow.deps           # Dependencies
├── Glow.lock           # Locked versions with checksums
├── lib/                # Library modules
│   └── helpers.glow
├── tests/              # Test files
│   └── test_app.glow
├── main.glow           # Entry point
└── README.md
```

### Using Libraries
```
# Import a library
use "lib/helpers.glow"

# Use standard library modules
use "lib/web.glow"
use "lib/orm.glow"
use "lib/tensor_utils.glow"
```

---

## Package Manager (gdu)

```bash
# Initialize project
gdu init

# Add a dependency
gdu add glowtensor "^1.0.0"

# Install dependencies
gdu install

# List installed packages
gdu list

# Verify checksums
gdu verify

# Search packages
gdu search web
```

---

## GlowLang Language Quick Reference

### Variables & Types
```
let x = 42              # Integer
let y = 3.14            # Float
let s = "hello"         # String
let b = true            # Boolean
let lst = [1, 2, 3]     # List
let d = {"key": "val"}  # Dictionary
let n = none             # None
```

### Functions
```
func greet(name):
    show "Hello, " + name
end

greet("World")
```

### Classes (Natural Language Syntax)
```
blueprint Animal:
    action init(name):
        self.name = name
    end
    action speak():
        show self.name + " speaks!"
    end
end

let cat = Animal("Cat")
cat.speak()
```

### Control Flow
```
# Natural conditionals
when x > 10:
    show "big"
eif x > 5:
    show "medium"
otherwise:
    show "small"
end

# Loops
every item in [1, 2, 3]:
    show item
end

for i from 1 to 10:
    show i
end

while x > 0:
    x = x - 1
end

repeat 5 times:
    show "hello"
end
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

### Error Handling
```
try:
    throw "something went wrong"
catch e:
    show "Caught: " + e
end
```

### HTTP Server
```
let server = http.listen(8080)
while true:
    let client = http.accept(server)
    http.send(client, "HTTP/1.1 200 OK\r\n\r\nHello!")
    http.close(client)
end
```

### Database
```
let db = sqlite.open("app.db")
sqlite.exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)")
sqlite.exec(db, "INSERT INTO users (name) VALUES ('Alice')")
let rows = sqlite.query(db, "SELECT * FROM users")
for each row in rows:
    show row["name"]
end
sqlite.close(db)
```

### Tensors (SIMD-Accelerated)
```
let a = tensor.zeros(1000)
let b = tensor.zeros(1000)
let c = tensor.add(a, b)
let dot = tensor.dot(a, b)
```

---

## Docker Deployment

```bash
cd deploy
docker build -t my-app -f Dockerfile ..
docker run -p 8080:8080 my-app main.glow
```

---

## Troubleshooting

### Build Errors
- **flex: not found** → `sudo apt-get install flex`
- **bison: not found** → `sudo apt-get install bison`
- **sqlite3.h: not found** → `sudo apt-get install libsqlite3-dev`
- **ssl/ssl.h: not found** → `sudo apt-get install libssl-dev`

### Runtime Issues
- **Segfault on large tensors** → Increase stack size: `ulimit -s unlimited`
- **Thread issues** → GlowLang uses GIL; CPU-bound parallelism is limited
- **Parser errors** → Check syntax; use `end` to close all blocks

---

## Links

- **Repository**: https://github.com/AnshuMishra1122003/GlowLang
- **VSCode Extension**: `editors/glowlang-vscode/`
- **Documentation**: `docs/`
- **Examples**: `examples/`
