# Shell Scripting with GlowLang

GlowLang can replace shell and batch scripts: the same script runs on Windows,
Linux and macOS, with real data structures, error handling and readable code.

```glow
#!/usr/bin/env glowlang
# deploy: build and upload the site.
import "shell"
import "cli"

let app = ArgParser("deploy", "Build and upload the site")
app.flag("--dry-run", "Show what would happen", "-n")
app.option("--env", "Target environment", "staging", "-e")
let args = app.parse_or_exit()

let branch = sh("git rev-parse --abbrev-ref HEAD")
if branch != "main" and args["--env"] == "prod":
    os.eprint("deploy to prod only from main (on " + branch + ")")
    os.exit(1)
end

fs.remove_all("dist")
fs.mkdirs("dist/assets")
for each f in fs.glob("site/**/*.css"):
    fs.copy(f, "dist/assets")
end
if sh_run(["npm", "run", "build"]) != 0:
    os.exit(1)
end

if args["--dry-run"]:
    show "would upload " + str(len(fs.walk("dist"))) + " files to " + args["--env"]
else:
    sh(["rsync", "-a", "dist/", "deploy@" + args["--env"] + ".example.com:/srv/site"])
end
```

## Running scripts

```bash
glowlang run deploy.glow --env prod
```

```bash
glowlang deploy.glow -n
```

A file that starts with a `#!` line does not need the `.glow` extension, so on
Linux and macOS a script can be made executable and run directly:

```bash
chmod +x deploy
```

```bash
./deploy --env prod
```

The arguments after the program file are `os.args` (a list of strings, Unicode
included on Windows), and `os.script` is the file's path. A program ends with
exit code 0, or 1 if an error is not caught; `os.exit(code)` ends it with any
code.

## Shipping a script as one file

`glowlang new mytool --cli` starts a command-line tool that is ready for
this: `mytool.glow` (arguments, `--help`, a table of results), `words.glow`
with the work, its tests in `test_words.glow` (`glowlang test`), and a
README. Change the work to your own and package it:

```bash
glowlang package deploy.glow
```

makes `deploy` (`deploy.exe` on Windows): one executable that holds GlowLang,
the program, and every module it uses: the program's own files, packages from
`gdu install`, and the standard libraries it imports. Copy it to a machine
without GlowLang and run it from any folder: `./deploy --env prod`. Its
arguments are `os.args` and `os.script` is the executable's path. `-o name`
picks another file name. It prints each module it packed:

```
  + helpers.glow (helpers.glow)
  + cli (/usr/local/lib/glowlang/lib/cli.glow)
Package created: deploy (3 files)
```

- Modules are found from `use` and `import` lines with the path written out.
  A module loaded by a path built while the program runs is not packed, and
  must be on disk next to it.
- The executable runs on the same kind of system it was made on, unless you
  name another with `--target`:

  ```bash
  glowlang package deploy.glow --target linux-x64     # also macos-arm64, macos-x64, windows-x64
  ```

  This downloads the released GlowLang runtime of the same version for that
  platform once (with `curl` and `tar`), keeps it in `~/.glowlang/runtimes`,
  and puts the program on it. It needs GlowLang 1.12.0 or later, and a build
  of GlowLang between releases gets the runtime of the release it is
  numbered as. `--runtime path/to/glowlang` uses a GlowLang binary you
  already have instead.
- On Windows the executable is not signed, so SmartScreen or Smart App Control
  may block it on other machines.

## Running commands (`import "shell"`)

A command is either a **string**, run by `/bin/sh` (or `cmd.exe` on Windows) so
pipes, redirection and wildcards work, or a **list** such as
`["git", "commit", "-m", message]`, run directly with each item as one
argument: nothing needs quoting and nothing in the arguments is interpreted.
Prefer lists for anything that includes user input or file names.

| Function | What it does |
|----------|--------------|
| `sh(cmd)` | Runs cmd and returns its output (with `\n` line endings, final newline removed). Raises if the command fails, with its exit code and error output. |
| `sh_run(cmd)` | Runs cmd with its output going straight to the terminal; returns the exit code. |
| `sh_ok(cmd)` | `true` when cmd exits with 0; the output is discarded. |
| `sh_lines(cmd)` | The output of `sh(cmd)` as a list of lines. |
| `pipe([cmd1, cmd2, ...])` | Feeds each command's output to the next, without a shell; returns the last one's output. |
| `quote(text)` | text quoted as one argument for a command *string* (`'...'` for `/bin/sh`, `"..."` for `cmd.exe`). |
| `which(name)` | The full path of a program on `PATH`, or `none`. |

Every function also takes the options of [`process.run`](process_guide.md):
`{"cwd": dir, "env": {...}, "input": text, "timeout": seconds}`. They are also
reachable as `shell.sh`, `shell.run`, `shell.ok`, `shell.lines`, `shell.pipe`,
`shell.quote` and `shell.which`. For long-running programs you talk to, use
`process.start` directly.

## Environment and directories (`os.`)

| | |
|---|---|
| `os.getenv(name, default)` / `os.env(name)` | A variable, or default (`none`) when unset. UTF-8 on every platform. |
| `os.env()` | Every variable, as a dict. |
| `os.setenv(name, value)`, `os.unsetenv(name)` | Also seen by programs started afterwards. |
| `os.cwd()`, `os.chdir(dir)` | The current directory. |
| `os.home()`, `os.temp_dir()` | The user's home and the temporary directory. |
| `os.platform` | `"windows"`, `"linux"` or `"macos"`. |
| `os.pid()` | This process's id. |
| `os.cpu_count()` | The number of CPU cores; see [`parallel.map`](process_guide.md#using-every-core-import-parallel) to use them. |
| `os.isatty(fd)` | Whether 0 (stdin), 1 (stdout) or 2 (stderr) is a terminal. |
| `os.read_stdin()` | Everything on standard input, for filters such as `cat data.txt \| glowlang run count.glow`. `input(prompt)` reads one line. |
| `os.write(...)`, `os.eprint(...)`, `os.ewrite(...)` | Print without a newline; print to standard error. |

## Files (`fs.`)

| | |
|---|---|
| `fs.read`, `fs.write`, `fs.append` | Whole-file text. |
| `fs.exists`, `fs.is_file`, `fs.is_dir` | What is at a path. |
| `fs.copy(src, dst)`, `fs.move(src, dst)` | dst may be a directory to copy or move into; `move` renames directories too. |
| `fs.mkdirs(path)` | Creates a directory and its parents (`mkdir -p`). |
| `fs.remove(path)`, `fs.remove_all(path)` | A file or empty directory; or a directory and everything in it (links inside are removed, not followed; a filesystem root is refused). |
| `fs.glob(pattern)` | Matching paths, sorted: `*`, `?`, `[abc]`, `[a-z]` within a name, `**` for any number of directories. Names starting with `.` are only matched by a pattern starting with `.`. |
| `fs.walk(dir)` | Every file below dir, sorted. |
| `fs.join(a, b, ...)`, `fs.basename`, `fs.dirname`, `fs.extension`, `fs.abspath` | Path pieces. `join` uses `/`, which Windows accepts. |

## Command-line interfaces (`import "cli"`)

`ArgParser` handles flags (`--verbose`, `-v`), options (`--env prod`,
`--env=prod`, `-e prod`), positional arguments, subcommands and `--help`:

```glow
import "cli"
let app = ArgParser("tool", "Does things")
app.flag("--verbose", "Say more", "-v")
app.option("--out", "Output directory", "build/", "-o")
app.positional("input", "Input file")
app.subcommand("check", "Only check the input")
let a = app.parse_or_exit()       # reads os.args
show a["_subcommand"], a["--verbose"], a["--out"], a["input"], a["_rest"]
```

`parse(list)` raises on a mistake; `parse_or_exit()` prints the problem and the
usage and exits with code 2, and prints the usage and exits for `--help`.
The library also has `ColorOutput()` (`success`, `info`, `warning`, `error`;
colored only on a terminal and when `NO_COLOR` is not set), `ProgressBar(total,
width)`, `TextTable()` and `Prompt()` (`ask`, `confirm`, `menu`). See
`examples/cli_app.glow`.
