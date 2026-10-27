# gdu (Glow Dependency Utility)

`gdu` is GlowLang's package manager. It is a single C program (`gdu.c`, linked with SQLite and
OpenSSL's libcrypto) that installs packages from the
[GlowLang-packages](https://github.com/anshumali19/GlowLang-packages) registry, tracks a
project's dependencies and can install the GlowLang toolchain itself.

## Commands

| Command | What it does |
|---------|--------------|
| `gdu init` | Create `Glow.deps` and the `.glow_pkg/` folder in the current project |
| `gdu install <pkg>[@version]` | Download a package and add it to `Glow.deps` |
| `gdu install` | Install everything listed in `Glow.deps` (pinned by `Glow.lock` when present) |
| `gdu add <pkg> [version]`, `gdu add --url <url>` | Add a dependency |
| `gdu remove <pkg>` | Remove a dependency and its file |
| `gdu resolve` | Resolve versions and write `Glow.lock` (with SHA-256 checksums) |
| `gdu verify [pkg]` | Check installed packages against their SHA-256 checksums |
| `gdu list`, `gdu info <pkg>`, `gdu search <query>`, `gdu outdated` | Inspect packages |
| `gdu cache list`, `gdu cache clean` | Manage the download cache |
| `gdu install glowlang` | Download the latest GlowLang release and add it to PATH |
| `gdu run <file>` | Run a GlowLang script |

## Files

* **`Glow.deps`** — one dependency per line: `name@version`, optionally followed by `(url)`.
  Lines starting with `#` are comments.
* **`Glow.lock`** — `name@version sha256:<hash>` per line, written by `gdu resolve`.
* **`.glow_pkg/<name>.glow`** — the installed package, inside the project.
* **`.glow_pkg/.cache/`** — downloaded versions, reused by later installs.

## Where packages come from

`gdu install <name>` first looks for the package in a local GlowLang `lib/` folder, then downloads
`packages/<name>/<name>.glow` from the registry on GitHub (via `curl`). If neither has it, the
install fails with an error.

## How programs load packages

`use "name"` in a GlowLang program finds, in order: `name.glow` next to the program or in the
current directory, an installed package in `.glow_pkg/name.glow`, and `name.glow` in the
`lib/` folder bundled with GlowLang.
