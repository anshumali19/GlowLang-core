# Changelog

All notable changes to the GlowLang VS Code extension.

## [2.0.0] — 2026-06-04

### Added
- **25+ code snippets** for functions, blueprints, loops, web servers, database models, AI patterns, HTTP clients, ORM, and ML pipelines
- **Run & Debug commands** with keybindings (Ctrl+Shift+R / Ctrl+Shift+D)
- **Profiler integration** — profile .glow files and open the profiler UI in a webview panel
- **Stack Visualizer** — open the VM stack visualizer directly in VS Code
- **Package manager commands** — init and install via command palette
- **Context menus** — right-click to run or profile .glow files
- **Title bar buttons** — play and debug icons for .glow files
- **Status bar** — GlowLang indicator with LSP status
- **Output channel** — dedicated GlowLang output panel
- **Task definitions** — VS Code task runner support
- **Enhanced TextMate grammar** — f-string interpolation, triple strings, method calls, blueprint definitions, pipe operators, hex literals
- **Improved language configuration** — folding markers, word patterns, enhanced on-enter rules
- **LSP restart command** — restart the language server without reloading VS Code

### Changed
- Upgraded package.json with full contributes section (commands, menus, keybindings, snippets, tasks)
- Enhanced language-configuration.json with folding support and smarter indentation
- Richer TextMate grammar with separate scopes for conditionals, loops, flow control, declarations, and I/O

## [1.0.0] — 2026-06-03

### Added
- Initial release
- Basic syntax highlighting
- LSP client integration
- Language configuration with brackets, auto-closing pairs, and indentation
