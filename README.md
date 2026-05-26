# GlowLang 1.0: Natural Language, Native Speed

GlowLang is a modern systems programming language that reads like English and runs like C. With the release of version 4.0, GlowLang introduces a custom **x86-64 Tier-1 JIT Compiler**, delivering performance that is **14x faster than Python 3.10**.

## Key Features
- **Natural Syntax**: `blueprint`, `action`, `can`, `use`, `when`.
- **High Performance**: Native machine code generation via custom Tier-1 JIT.
- **Hardware Aware**: Register-mapped stack and NaN-boxed memory management.
- **Industrial Concurrency**: OS threads with a Global Interpreter Lock (GIL) and `spawn`.
- **Secure Networking**: Native bindings for **OpenSSL**, **Net**, and **HTTP**.
- **Modern OOP**: Hidden Classes (Shapes), Traits, and single inheritance.
- **Robustness**: Nested `try-catch-throw` with safe C-to-Glow stack unwinding.

## Quick Start

```bash
# Build (requires flex, bison, gcc, openssl)
make

# Run the Industrial Stress Test
bin/glowlang tests/complex_industrial_test.glow
```

## VS Code Integration (Version 1.0.0)
GlowLang 1.0 comes with a high-performance VS Code extension.

### Installation
1. Navigate to `editors/vscode`.
2. Install dependencies: `npm install`.
3. Compile the extension (optional): `npm run compile`.
4. Open the folder in VS Code and press `F5` to start debugging, or package it using `vsce package`.
5. **Configuration**: Ensure the `glowlang` binary is in your PATH, or set the `glowlang.path` setting in VS Code to point to `bin/glowlang`.

## Performance (10M Iterations)
| Engine | Time (s) | Speedup |
|--------|----------|---------|
| Python 3.10 | 0.589s | 1x |
| GlowLang 1.0 (Interpreter) | 0.158s | 3.7x |
| **GlowLang 1.0 (JIT)** | **0.042s** | **14x** |

## Documentation
- [Project History](docs/PROJECT_HISTORY.md)
- [Industry Readiness Status](docs/INDUSTRY_READY.md)

## Directory Structure
- **`src/`**: Core implementation (C, Flex, Bison).
- **`tools/gdu/`**: Glow Dependency Utility (Package Manager).
- **`bin/`**: Compiled binaries.
- **`tests/`**: Language test suite.
- **`docs/`**: Documentation and history.

---
*GlowLang 1.0: The Industrial Revolution of Natural Programming.*
