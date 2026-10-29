# Running other programs: the `process` module

`process` runs other programs from GlowLang: Python, Node.js, compilers, git,
shell commands, or another GlowLang program. It works the same on Windows,
Linux and macOS. No `import` is needed.

| Function | What it does |
|---|---|
| `process.run(cmd, opts)` | Run a program to the end and return its output |
| `process.start(cmd, opts)` | Start a long-running program and return a handle |
| `process.send(h, text)` | Write text to its standard input |
| `process.read_line(h, timeout)` | Read the next line it prints (`none` at the end) |
| `process.read_all(h, timeout)` | Read everything it prints until it closes its output |
| `process.close_input(h)` | Close its standard input (it reads end of input) |
| `process.wait(h, timeout)` | Wait for it to exit and return the exit code (`none` on timeout) |
| `process.alive(h)` | `true` while it is running |
| `process.pid(h)` | Its process ID |
| `process.kill(h)` | Stop it and everything it started |
| `process.close(h)` | Stop it if it is still running and free the handle |
| `process.find(name)` | Full path of a program on `PATH`, or `none` |
| `process.executable` | Path of the running `glowlang` |
| `process.cwd()` | The current folder, as an absolute path |

## The command

A **list** runs the program directly, with no shell. Each item reaches the
program as exactly one argument, whatever it contains, so text from users or
files is safe to pass:

```glow
let r = process.run(["git", "log", "-1", "--format=%s"], {})
show r["stdout"].trim()
```

A **string** is run by the system shell (`/bin/sh -c` on Linux and macOS,
`cmd.exe /c` on Windows), so pipes and redirection work. Shell syntax differs
between systems, and text pasted into a shell string can run as a command,
so prefer the list form.

```glow
let r = process.run("echo hello", {})
```

On Windows, a program is found on `PATH` with the extensions in `PATHEXT`, so
`["npm", "install"]` finds `npm.cmd`. Arguments that `cmd.exe` would misread
for a `.bat` or `.cmd` file (containing `"`, `%` or a line break) are
refused with an error rather than passed on.

## `process.run`

```glow
let r = process.run(["python", "-c", "print(6 * 7)"], {"timeout": 10})
when r["ok"]:
    show "Python says", r["stdout"].trim()
otherwise:
    show "failed with code", r["code"], r["stderr"]
end
```

The result is a dict:

| Key | Value |
|---|---|
| `code` | Exit code. On Linux and macOS a program killed by a signal gives minus the signal number |
| `stdout` | Everything the program printed |
| `stderr` | Everything it printed to standard error |
| `ok` | `true` when the exit code is 0 and it did not time out |
| `timed_out` | `true` when the timeout stopped it |

Options (all optional):

| Option | Meaning |
|---|---|
| `input` | Text written to the program's standard input, which is then closed. Without it, the program shares GlowLang's standard input |
| `cwd` | Folder to run in |
| `env` | Dict of environment variables added over the current ones. A `none` value removes a variable |
| `timeout` | Seconds. When it passes, the program and everything it started are stopped and `timed_out` is `true` |
| `stdout` | `"pipe"` (default, captured), `"inherit"` (shown in the terminal) or `"null"` (discarded) |
| `stderr` | `"pipe"` (default), `"stdout"` (mixed into `stdout`), `"inherit"` or `"null"` |

Output of any size is read while the program runs, so large input and output
never block. Line endings are left as the program wrote them; use
`.lines()` to split output into lines on every system.

## Long-running programs

`process.start` keeps a program running so GlowLang can talk to it, one
request and reply at a time. Its standard input and output are pipes; its
standard error goes to the terminal by default (`"stderr": "null"` discards
it). `cwd` and `env` work as for `run`.

```glow
let py = process.start(["python", "-u", "worker.py"], {})
process.send(py, "{\"op\": \"add\", \"a\": 2, \"b\": 3}\n")
let reply = json.parse(process.read_line(py, 30))
show reply["result"]
process.close(py)
```

- `read_line` returns one line without its line ending, or `none` once the
  program has closed its output. With a timeout it raises an error if no
  full line arrives in time; without one it waits as long as it takes.
- `send` returns `false` once the program has stopped reading.
- `close_input` tells the program there is no more input; many programs then
  finish and exit.
- Programs still running when GlowLang exits are stopped.
- Every handle uses system resources until `process.close`.

Blocking calls (`run`, `read_line`, `read_all`, `wait`, `send`) let other
GlowLang threads run while they wait.

## Working with other languages

For Python and JavaScript, the [`bridge` library](bridge_guide.md) does this
for you: `bridge.python().call("math.sqrt", [16])`. The rest of this section
is for other languages, or for writing your own protocol.

Any language that can read and write lines can be a GlowLang worker. Sending
one JSON object per line is a simple, reliable protocol.

A Python worker (`worker.py`):

```python
import json, sys

for line in sys.stdin:
    req = json.loads(line)
    if req["op"] == "add":
        out = {"result": req["a"] + req["b"]}
    else:
        out = {"error": "unknown op " + req["op"]}
    print(json.dumps(out), flush=True)
```

A Node.js worker (`worker.js`); for TypeScript, compile it with `tsc` first
and run the `.js` file:

```js
const rl = require("readline").createInterface({ input: process.stdin });
rl.on("line", (line) => {
  const req = JSON.parse(line);
  console.log(JSON.stringify({ result: req.a + req.b }));
});
```

From GlowLang:

```glow
let node = process.start(["node", "worker.js"], {})
process.send(node, json.stringify({"op": "add", "a": 2, "b": 3}) + "\n")
show json.parse(process.read_line(node, 30))["result"]
process.close(node)
```

Tips:

- Make the worker flush after every reply (`flush=True` or `python -u` in
  Python; `console.log` already does in Node), or replies wait in its buffer.
- Keep logging in the worker on standard error, so it never mixes with
  replies on standard output.
- Use `process.find("python")` to check a language is installed before
  starting it.

## Errors

These raise an error you can catch with `try` / `catch`: the program is not
found, `cwd` is not a folder, an option has the wrong type, a handle is
closed, or `read_line` / `read_all` times out. A program that runs and fails
does not raise: check `ok` or `code`.

```glow
try:
    process.run(["no-such-tool"], {})
catch e:
    show e    # process.run: program not found: no-such-tool
end
```
