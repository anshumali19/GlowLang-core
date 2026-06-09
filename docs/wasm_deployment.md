# GlowLang WASM Deployment Guide

## Building the WASM Target

GlowLang compiles to WebAssembly using Emscripten:

```bash
# Install Emscripten
# See: https://emscripten.org/docs/getting_started/downloads.html

# Build WASM
make wasm

# Output files:
# bin/glowlang.js    - JavaScript loader
# bin/glowlang.wasm  - WebAssembly binary
```

## Running the Playground

```bash
# Deploy WASM artifacts
bash scripts/deploy_wasm.sh

# Serve locally
python3 -m http.server 8080 -d web/playground
# Open http://localhost:8080
```

## Embedding in Web Apps

```html
<script src="glowlang.js"></script>
<script>
Module.onRuntimeInitialized = function() {
    // Execute GlowLang code
    Module.ccall('glow_main', 'number', ['number', 'number'], [0, 0]);
};
</script>
```

## WASM Limitations

- **No filesystem access**: `fs.read/write` are not available in WASM
- **No threading**: `spawn` is not supported (SharedArrayBuffer required)
- **No native HTTP**: `http.get/post` use browser `fetch()` instead of sockets
- **No SQLite**: ORM operations are not available
- **Memory limit**: Default 256MB heap, configurable via Emscripten flags
- **No JIT**: The JIT compiler is disabled in WASM builds

## Static Hosting

The playground is a static site. Deploy to any static hosting:

```bash
# GitHub Pages
cp -r web/playground/ docs/playground/
git add docs/playground/ && git commit -m "Deploy playground"
git push

# Netlify / Vercel
# Point build output to web/playground/
```
