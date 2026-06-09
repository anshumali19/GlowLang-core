# GlowLang Ecosystem Guide

## Module System

GlowLang uses the `use` keyword exclusively for module imports. **Do not use `import` or `require`.**

```glow
use "lib/web.glow"
use "lib/orm.glow"
use "lib/tensor_utils.glow"
```

## Standard Library

| Module | Description | Key Functions |
|--------|-------------|---------------|
| `os` | Operating system | `os.time()`, `os.exit(code)`, `os.sleep(ms)`, `os.system(cmd)` |
| `math` | Mathematics | `math.abs()`, `math.min()`, `math.max()`, `math.sqrt()`, `math.floor()`, `math.ceil()`, `math.round()` |
| `fs` | Filesystem | `fs.read(path)`, `fs.write(path, data)`, `fs.exists(path)`, `fs.size(path)`, `fs.mkdir(path)`, `fs.remove(path)`, `fs.list(path)` |
| `json` | JSON handling | `json.parse(str)`, `json.stringify(val)` |
| `http` | HTTP client | `http.get(url)`, `http.post(url, body)` |
| `tensor` | Tensor ops | `tensor.create(n)`, `tensor.zeros(n)`, `tensor.ones(n)`, `tensor.dot(a, b)` |

## Package Manager (gdu (Glow Dependency Utility))

```bash
gdu init                   # Initialize project
gdu add web                # Add dependency
gdu add web 1.2.0          # Add with version
gdu install                # Install all deps
gdu resolve                # Generate Glow.lock
gdu verify                 # Verify SHA-256 checksums
gdu list                   # List installed
gdu search <query>         # Search registry
gdu outdated               # Check for updates
gdu cache clean            # Clear cache
```

## Project Structure

```
my_project/
├── Glow.deps          # Dependencies
├── Glow.lock          # Locked versions with SHA-256
├── .glow_pkg/         # Installed packages
│   └── .cache/        # Download cache
├── lib/               # Local libraries
├── tests/             # Test files
└── main.glow          # Entry point
```
