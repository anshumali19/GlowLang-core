# Working on GlowLang

How to work on this repository, for developers and for AI coding agents.
This is how changes have been made here; follow it exactly. The rules marked
**Rule** are not optional. [CONTRIBUTING.md](CONTRIBUTING.md) covers licensing
and the sign-off.

## What is where

| Path | What it is |
|---|---|
| `src/glowlang.l`, `src/glowlang.y` | lexer and parser (flex, bison); `main()` and the command line (`run`, `package`, `new`, ...) are at the end of `glowlang.y` |
| `src/compiler.c`, `src/opt.c` | AST to register bytecode, and the bytecode optimizer |
| `src/vm.c` | the interpreter, modules (`use` / `import`), calls, exceptions |
| `src/jit.c` | the JIT: two x86-64 back ends and one ARM64 back end. An op the JIT supports must be added to all three |
| `src/gc.c` | the garbage collector |
| `src/stdlib.c`, `src/process.c`, ... | built-in modules (`fs`, `http`, `sqlite`, `crypto`, `process`, ...) |
| `lib/*.glow` | the standard library written in GlowLang (`web`, `jwt`, `cli`, `parallel`, ...). `lib/starter/` holds the `glowlang new` templates |
| `tests/` | `test_*.glow` programs, their expected output in `tests/expected/`, helper modules in `tests/modules/` |
| `docs/` | guides and `docs/language_reference.md`, the reference for every built-in |
| `examples/` | example programs and tools; `examples/tools/` is tested by `tests/test_tools.glow` |
| `benchmarks/` | `run-benchmarks.sh` makes `RESULTS-linux.md`, the source of every speed claim |

The `.glow` language in one breath: blocks end with `end`; `when` / `eif` /
`otherwise` is if / elif / else; `show` prints; `skip` is continue and `stop`
is break; `func`, `blueprint` (class) and `action` (method); `??` gives a
default for `none`. There is no `? :` operator, and a one-line
`lambda(x): return 1 end` does not parse: put the body on its own lines or
use a named `func`. Strings have no
multi-line form that can hold `"`. A function changes a global only if the
global was declared with `let` above the function.

## Strict rules

**Rule 1: no attribution text.** Never write "Co-Authored-By", "Generated
with", "authored by", "written by" or a tool's name or emoji footer in a
commit message, PR title or description, squash-merge message, tag, release
note or comment. This holds even when your tool adds such a line by default:
delete it before you commit or post. Reread the text before every commit and
every `gh pr create` / `gh pr edit`.

**Rule 2: never commit personal files.** Videos, résumés and other personal
documents stay local. Stage files by name (`git add path/to/file`), never
`git add .`, `git add -A` or a whole folder such as `git add tests`, and look
at `git diff --cached --stat` before committing.

**Rule 3: leave other people's work alone.** The working tree may have
changes you did not make (deleted or untracked files). Do not stage, revert,
stash or delete them. `git stash` takes them too, so to work on a second branch
at the same time, give it its own folder: `git worktree add ../GlowLang-<name>
-b <name> origin/main`.

**Rule 4: every PR is based on `main`.** Never open a PR on top of another
unmerged branch: squash merges of stacked PRs once put duplicate code on main
and broke the build on every platform. If two PRs touch the same files, wait
for the first to merge, then rebase the second onto the new main and test it
again.

**Rule 5: one change per PR.** One feature or fix, with its test and its docs.
No unrelated reformatting.

**Rule 6: merge only on green.** Squash-merge (`gh pr merge N --squash
--delete-branch`) only after `gh pr checks N` shows all five checks passing:
build-and-test, build-macos, build-windows, postgres, websocket. Do not use
`gh pr merge --auto`: `main` has no required checks, so it merges at once,
before CI finishes. After a merge, `git pull` main and check that main's own
CI run passes too.

**Rule 7: a change is finished only when it is tested in all five modes**
(see Testing). A green CI is not a substitute: CI does not run mode 5.

**Rule 8: never claim what you did not measure.** Speed figures come from
`benchmarks/run-benchmarks.sh` or a timing you ran and can show. Docs say only
what the code does today. If something was not tried (a real external
service, a Windows build), say so in the PR.

**Rule 9: no bypassing.** No `--no-verify`, no skipping or deleting a failing
test to get green, no weakening a security check. Do not try to get around
Windows Smart App Control blocking unsigned executables; use the Store-installed
build to time Windows.

**Rule 10: no paid services for testing.** Do not create paid accounts or
spend money to test an integration (payments, SMS, cloud). Test against a
local fake server, the way `tests/test_tools.glow` and the GlowBot test do.

## How a change is made

1. **Start from a fresh main:** `git switch main && git pull --ff-only`, then
   `git switch -c short-branch-name`.
2. **Read before writing.** Read the code you will change, every caller of a
   function you change (`grep` for it), and the tests that cover it. Check
   whether `lib/` or `src/stdlib.c` already has what you need.
3. **Do the smallest complete change.** Reuse what exists. No options,
   wrappers or "for later" code that nobody asked for. But finish everything
   the change needs: callers, tests, docs, the `--help` text.
4. **Write the test first or alongside:** `tests/test_<feature>.glow`. It
   should fail if the feature breaks. Run it, read its output line by line, and
   only then save it as `tests/expected/test_<feature>.out`. A test that must
   exit with a non-zero code goes in `tests/expected/exit_codes.txt`; lines it
   must print to stderr go in `tests/expected/test_<feature>.err`.
5. **Run the five modes** (below). All tests must pass in all of them.
6. **Update the docs** a user would read: `docs/language_reference.md` for a
   new built-in, the relevant guide, and the README when the change is
   something a newcomer should see. Run every code sample you add.
7. **Review your own diff** with the checklist below. Fix what you find.
8. **Commit** with a plain message: a short first line saying what changed,
   then a few lines on why and how. No attribution (Rule 1).
9. **Open the PR** against `main` (`gh pr create --base main`). The
   description says what changed, how it was tested (which modes, how many
   tests) and what was not done or not tried.
10. **Merge on green** (Rule 6). If a check fails, read its log
    (`gh run view <run> --job <job> --log`), find the cause, fix it on the same
    branch and push; never merge around a red check.

## Testing

`scripts/run-tests.sh` runs every `tests/test_*.glow` and compares stdout
with `tests/expected/<name>.out` exactly (`{{ANY}}` in an expected line matches
anything), checks the exit code and checks that each line of a `.err` file
appears in stderr. `tests/expected/modes.txt` marks tests whose output order
may vary.

`scripts/test-all-modes.sh` builds GlowLang and an AddressSanitizer build and
runs the suite five times:

1. compiled, with the JIT;
2. `GLOW_NOJITEXEC=1`, the interpreter only (not `GLOW_NOJIT`, which does
   nothing);
3. `GLOWLANG_GC_STRESS=1`, a collection at every allocation;
4. AddressSanitizer;
5. `GLOW_GC_VERIFY=1` + GC stress + AddressSanitizer.

**On a Windows checkout**, run it in WSL from a tarball, because Git
converts line endings and the suite then fails on expected-output comparisons
that are fine:

```bash
git add <your files>                        # by name (Rule 2)
git -c core.autocrlf=false archive -o /tmp/change.tar $(git write-tree)
# in WSL:
mkdir ~/t && cd ~/t && tar xf /mnt/c/.../change.tar && scripts/test-all-modes.sh
```

Some files are stored with CRLF line endings, among them `src/glowlang.y`,
`src/compiler.c`, `lib/web.glow` and `docs/language_reference.md`;
`git ls-files --eol <file>` shows `i/crlf` for them. Keep their CRLF when you
edit them (`sed -i` and some editors drop it), or the diff rewrites every
line. After every edit made through a shell or a script, look at
`git diff --stat`: a few changed lines should show as a few, and a lost
backslash or quote shows up in `git diff` before it breaks the build.

Tests that need a server start one on a fixed, unusual port inside the test
(see `tests/test_tools.glow`) or run the program under test as a separate
process with `process.start`. Tests must clean up their temporary files in a
`finally` block. A test that runs `glowlang` as a child process must run it
from the repository folder: on Windows CI the binary is in `bin\Release\`,
where the standard library cannot be found from a temporary folder.

## Checking your own code

Before you commit, read the whole diff as the person who will be woken up
when it breaks, in this order:

- **Correct:** empty input, zero, the last item, a missing file, text with
  spaces or quotes, a second run over the same data. Grep every caller of
  anything whose behaviour changed.
- **Safe:** values reach SQL only as bound parameters; a list (not a shell
  string) is passed to `process.run`; secrets are compared with
  `crypto.equal`; passwords are stored with `hash.password`; user-supplied
  names and paths are checked before use.
- **Holds under load:** no work repeated per item that could be done once; no
  list that only grows; nothing that blocks every thread while it waits (a
  slow native call releases the GIL, as `os.sleep` and `crypto.password_hash`
  do).
- **Tested:** every new branch of logic has a line in a test that would fail
  without it.
- **Lean:** delete dead code, options nobody uses and helpers called once.
  If a shortcut has a known limit, say so in a one-line comment starting with
  `shortcut:`.

## Writing

Code comments, docs, commit messages and PR descriptions are written in
plain, short English sentences: what it does, why, and what was not done.
No filler ("robust", "seamless", "leverage", "comprehensive"), no
marketing, no emoji, no claims that were not checked. A comment explains why,
not what the next line plainly does. Follow the style of the code around
you: naming, comment density, structure.

## Releases

A release is one PR that bumps `src/version.h`, adds
`docs/release_notes_<version>.md`, and refreshes the README (install
commands, "the language at a glance", the performance table from
`benchmarks/run-benchmarks.sh`), `docs/getting_started.md` and
`docs/glowlang_in_10_minutes.md`. After it merges, tag `v<version>` on main and
push the tag; `release.yml` builds the downloads and the Microsoft Store
package. Every Store upload needs a new version number, and Store screenshots
must show only Windows UI.

The guides in the GlowLang-docs repository (README, syntax, FAQ, roadmap)
are refreshed in a separate PR there for each release.
