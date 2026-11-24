# GlowLang 1.12.0

## New

- **`glowlang new myapi`: a web API in one step.** It writes a working
  project: sign-up, login with JWT tokens, passwords stored as salted
  hashes, notes in SQLite, and `test_app.glow`, which starts the app and
  calls every endpoint. See [the GlowWeb tutorial](glowweb_tutorial.md#starting-a-project).

  ```bash
  glowlang new myapi
  cd myapi
  glowlang run test_app.glow    # 16 checks
  glowlang watch app.glow       # http://localhost:8000, restarted on every save
  ```

  `glowlang new mytool --cli` starts a command-line tool instead: arguments
  with `--help`, the work in its own module with tests, ready for
  `glowlang package`.

- **`glowlang watch app.glow`** runs a program and runs it again each time a
  `.glow` file in its folder is saved.

- **`glowlang test`** finds `test_*.glow` files and runs each top-level
  `func test_...()` in them, showing what failed, the error and what the
  test printed; the exit code is 1 when anything failed. See
  [Testing your code](testing_your_code.md).

- **`glowlang fmt`** re-indents `.glow` files the same way everywhere;
  `--check` reports files that would change, for CI. It only rewrites a
  file when the result still passes `glowlang check`.

- **`glowlang package` packs every module the program uses** (its own
  files, `gdu` packages, the standard library) and passes it its arguments,
  so the one file really runs anywhere. **`--target linux-x64`** (or
  `macos-arm64`, `macos-x64`, `windows-x64`) builds for another system with
  the released runtime of the same version, and `--runtime` packs onto a
  GlowLang binary you name. See
  [Shipping a script as one file](shell_scripting.md#shipping-a-script-as-one-file).

- **Five tools to copy** in [examples/tools](../examples/tools/README.md): a
  log summarizer, a CSV cleaner, a folder backup, an API checker and a bulk
  renamer, and **GlowBot** ([examples/glowbot](../examples/glowbot/README.md)),
  which writes an email with you and sends it from your Gmail after you type
  yes.

- **Error messages that say how to fix the mistake**: "did you mean" for
  misspelled names, methods and module functions; the block that is missing
  its `end`, found from the indentation; and GlowLang's word when another
  language's slipped in (`def`, `var`, `When`, braces).

- **Line editing in the REPL** on Linux and macOS: arrow keys, Home/End,
  Ctrl+A/E/U/K/L/C/D and a history that is kept between sessions
  (`~/.glowlang_history`). The Windows console already did this.

- **New string methods:** `pad_start` / `pad_end` (`"7".pad_start(3, "0")`
  is `"007"`), `remove_prefix` / `remove_suffix`, `capitalize` and `title`.

- **Passwords:** `hash.password(pw)` and `hash.check_password(pw, stored)`
  (PBKDF2-SHA256, 600,000 iterations, the format Django uses).

- **GlowWeb:** routes with `:name` parts (`/notes/:id`, in `req.params`), and
  query strings and route parts are URL-decoded (`http.url_decode`).

- `process.start` takes `"stdout": "inherit"` or `"null"`, and on Linux and
  macOS Ctrl+C now also stops the programs `process.start` ran.
  `fs.stat` gives `modified`.

## Faster

- **Concurrent servers, up to 22× faster.** A spawned thread whose function
  has returned now waits for the next `spawn` and is reused, instead of a
  new OS thread and VM stack being made each time (about 0.5 ms each); and
  the GIL is handed over after a 1 ms switch interval instead of at every
  call while anyone waits. The same small JSON API, new connection per
  request, measured with [benchmarks/web](../benchmarks/web/README.md)
  (Linux under WSL 2, i7-14650HX):

  | | 1 client | 8 clients |
  |---|---|---|
  | GlowWeb 1.11.0 | 1,636 req/s | 412 req/s |
  | GlowWeb 1.12.0 | 11,372 req/s | 9,324 req/s |
  | Python 3.14 `http.server` | 3,531 req/s | 3,532 req/s |
  | Node.js 22 `http` | 11,041 req/s | 16,588 req/s |

- The CPU benchmarks are unchanged from 1.11.0 (1.4× to 7.1× faster than
  CPython; [benchmarks/RESULTS-linux.md](../benchmarks/RESULTS-linux.md)).

## For contributors

- [AGENTS.md](../AGENTS.md) is the working guide: the rules, how a change
  is made, tested in five modes (`scripts/test-all-modes.sh`) and checked.
