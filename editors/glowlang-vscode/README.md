# GlowLang for Visual Studio Code

VS Code support for the **GlowLang** programming language: syntax highlighting, snippets, run/debug/profile commands and the GlowLang language server.

## Features

### Syntax Highlighting
Rich TextMate grammar with support for:
- All GlowLang keywords (`func`, `blueprint`, `action`, `when`, `eif`, `otherwise`, `for each`, etc.)
- String interpolation with `f"Hello {name}"`
- Triple-quoted strings, comments (`#`, `//`, `/* */`)
- Blueprint/class definitions, method calls, operators
- Numeric literals (integer and float)

### Snippets
26 snippets, including:
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
- **GlowLang: Profile Current File** runs `glowlang profile`, which prints how often each line ran
- Right-click context menu for run and profile
- Title bar play/debug buttons

### LSP Integration
- Syntax errors shown as you type (the server is `glowlang --lsp`)
- Completion of keywords and built-ins, hover and go-to-definition
- Restart LSP via command palette

### Package Manager
- **GlowLang: Initialize Package** and **Install Dependencies** run `gdu init` and `gdu install` (gdu is found next to `glowlang`, or on PATH)

## Requirements

- GlowLang: on Windows install it from the Microsoft Store (`winget install --id 9P2D8F466LBX --source msstore`); elsewhere use a release or a source build. Set `glowlang.path` if `glowlang` is not on PATH.
- VS Code 1.75.0 or later

## Extension Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `glowlang.path` | `glowlang` | Path to the glowlang executable |
| `glowlang.enableLsp` | `true` | Enable LSP server |
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

- Diagnostics report syntax errors; errors that only appear when the program runs (an undefined name, a wrong type) are not shown in the editor

## License

Part of the GlowLang project — licensed under the Apache License 2.0 with the Commons Clause (see the repository's LICENSE).
