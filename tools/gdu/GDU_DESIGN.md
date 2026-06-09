# gdu (Glow Dependency Utility) Design Document

## Overview
`gdu` is the package manager for GlowLang. It handles remote module fetching, dependency resolution, and project reproducibility.

## Core Features
1. **`gdu add <url>`**: Adds a remote dependency (Git or HTTPS).
2. **`gdu install`**: Installs dependencies listed in `Glow.deps` (or similar).
3. **`Glow.lock`**: Ensures reproducible builds by locking versions/shas.
4. **Local Caching**: Stores downloaded modules in a local cache (e.g., `~/.glow/pkg`).

## Implementation Strategy
- Written in C for performance and to keep the ecosystem self-contained.
- Uses `curl` (via `system` or `libcurl`) for fetching.
- Integrates with the existing `import` system by resolving remote URLs to local cache paths.

## Usage in GlowLang
GlowLang will be updated to check the `gdu` cache if a local file is not found.

```glow
import "https://github.com/user/math.glow"
```

The compiler will consult `Glow.lock` to find the local path for this URL.
