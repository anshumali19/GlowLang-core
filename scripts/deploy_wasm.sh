#!/usr/bin/env bash
# Deploy GlowLang WASM playground
set -euo pipefail
cd "$(dirname "$0")/.."

echo "Building WASM target..."
make wasm

echo "Copying artifacts to web/playground/..."
mkdir -p web/playground
cp bin/glowlang.js web/playground/
cp bin/glowlang.wasm web/playground/

echo "WASM playground ready at web/playground/index.html"
echo "Serve with: python3 -m http.server 8080 -d web/playground"
