# GlowLang for Visual Studio Code

Full-featured VS Code extension for the **GlowLang** programming language — an industrial-grade JIT-compiled language with natural-language syntax, native networking, and GPU-ready tensor operations.

## Features

### Syntax Highlighting
Rich TextMate grammar with support for:
- All GlowLang keywords (`func`, `task`, `blueprint`, `action`, `when`, `otherwise`, `for each`, etc.)
- String interpolation with `f"Hello {name}"`
- Triple-quoted strings, comments (`#`, `//`, `/* */`)
- Blueprint/class definitions, method calls, operators
- Numeric literals (integer, float, hex)

### Snippets
25+ snippets for rapid development:
- `func` — Function definition
- `blueprint` — Class/blueprint with init and methods
- `foreach` — For each loop
- `when` / `whenotherwise` — Conditionals
- `trycatch` — Error handling
- `webserver` — Full web server boilerplate
- `dbmodel` — Database model setup
- `neuralnet` — AI neural network template
- `httpget` — HTTP client request
- `mlpipeline` — ML pipeline setup
- And more...

### Run & Debug
- **Ctrl+Shift+R** — Run current .glow file
- **Ctrl+Shift+D** — Debug current .glow file
- Right-click context menu for run and profile
- Title bar play/debug buttons

### LSP Integration
- Real-time diagnostics and error checking
- Auto-completion (when LSP server is available)
- Restart LSP via command palette

### Profiler & Stack Visualizer
- Open profiler UI directly in VS Code
- Visualize VM stack frames in the stack visualizer
- Both tools run as webview panels

### Package Manager
- Initialize new GlowLang projects
- Install dependencies

## Requirements

- GlowLang compiler (`bin/glowlang`) built from source
- VS Code 1.75.0 or later

## Extension Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `glowlang.path` | `glowlang` | Path to the glowlang executable |
| `glowlang.enableLsp` | `true` | Enable LSP server |
| `glowlang.profilerPort` | `9229` | Profiler web UI port |
| `glowlang.trace.server` | `off` | LSP trace level |

## Installation

### From Source
```bash
cd editors/glowlang-vscode
npm install
# Then: Extensions → Install from VSIX, or symlink to ~/.vscode/extensions/
```

### Development
```bash
cd editors/glowlang-vscode
npm install
code .
# Press F5 to launch Extension Development Host
```

## Known Issues

- LSP server requires `--lsp` flag support in the glowlang binary
- Profiler requires `--profile` flag for data generation

## License

Part of the GlowLang project — released under the GlowLang Sovereign License (GSL) v1.0.
