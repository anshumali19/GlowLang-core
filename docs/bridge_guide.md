# Using Python and JavaScript from GlowLang: `bridge`

`bridge` lets GlowLang call Python and JavaScript (Node.js, and TypeScript
through Node) directly: standard library functions, packages such as NumPy
or lodash, and your own files.

```glow
import "bridge"

let py = bridge.python()
show py.call("math.sqrt", [16])                      # 4
show py.call("json.dumps", [{"a": 1}], {"indent": 2})

let js = bridge.node()
show js.call("path.join", ["src", "app.js"])        # src/app.js
```

You need Python 3 and/or Node.js installed. GlowLang finds them on `PATH`.

## How it works

Each bridge starts one worker program (`lib/bridge/python_worker.py` or
`lib/bridge/node_worker.js`, which ship with GlowLang) and sends it one JSON
message per line. The worker keeps running, so imports and variables stay
loaded between calls and each call costs about as much as a function call
plus a JSON round trip.

- **Copied values:** numbers, strings, bools, `none`, lists and dicts are
  copied in both directions. Python tuples become lists. Integers stay
  exact, including big ones. NaN and infinity become `none`.
- **References:** every other value stays in the worker and comes back as a
  `ForeignRef`. Examples are a NumPy array, a Python object, a set, a
  `collections.Counter`, a JS class instance, a `Map`, a module or a
  function. You can call its methods, read its fields, pass it back as an
  argument, or copy it into GlowLang with `.value()`.

```glow
let np = py.load("numpy")
let a = py.call("numpy.array", [[1.5, 2.5, 3.5]])
show a.type_name           # numpy.ndarray
show a.call("mean")        # 2.5
show a.value()             # [1.5, 2.5, 3.5]
show py.call("numpy.dot", [a, a])
```

## Bridge methods

| Method | What it does |
|---|---|
| `call(name, args, kwargs)` | Call a function by dotted name and return the result. Examples: `"math.sqrt"`, `"os.path.join"`, `"mymodule.run"`, or a name defined with `exec`. `kwargs` are Python keyword arguments |
| `get(name)` | Read a value: `py.get("sys.version")`, `js.get("process.platform")` |
| `set(name, value)` | Set a global, or an attribute such as `"config.debug"` |
| `eval(code)` | Evaluate an expression: `py.eval("[i * i for i in range(5)]")` |
| `exec(code)` | Run statements, for example to define functions or classes for later calls |
| `load(name)` | Load a module or package and return it as a `ForeignRef`: `"numpy"`, `"lodash"`, `"./util.js"` |
| `alive()` | `true` while the worker is running |
| `close()` | Stop the worker. Workers also stop when GlowLang exits |

`version` is the interpreter's version. `last_traceback` holds the full
traceback of the last error.

## `ForeignRef` methods

| Method | What it does |
|---|---|
| `call(name, args, kwargs)` | Call a method: `counter.call("add", [5])` |
| `invoke(args, kwargs)` | Call the value itself (a function or class) |
| `get(name)` / `set(name, value)` | Read or write a field |
| `value()` | Copy the whole value into GlowLang. Arrays and sets become lists, maps become dicts |
| `release()` | Let the worker free it. Long-running programs that create many references should release them |

`type_name` (for example `numpy.ndarray` or `Map`) and `text` (a short
printed form) describe the value.

## Your own code

Modules are found in the folder GlowLang runs in, or in the `cwd` option.

```python
# shapes.py
class Circle:
    def __init__(self, r):
        self.r = r
    def area(self):
        return 3.14159 * self.r ** 2
```

```glow
let c = py.call("shapes.Circle", [2])
show c.call("area")
```

For JavaScript, `"shapes.area"` loads `./shapes.js` when no installed package
has that name. Use `js.load("./lib/shapes.js")` for other paths. ES modules
(`.mjs`, or `"type": "module"`) load too. Async functions are awaited, and
classes are created with `new`.

**TypeScript:** Node.js 23.6 and newer run `.ts` files directly. On Node 22,
pass the flag:

```glow
let ts = bridge.node({"node_args": ["--experimental-strip-types"]})
show ts.load("./math.ts").call("add", [2, 3])
```

## Options

```glow
bridge.python({"timeout": 30, "cwd": "scripts"})
```

| Option | Meaning |
|---|---|
| `python` / `node` | Interpreter to use. Otherwise `$GLOW_PYTHON` / `$GLOW_NODE`, then `python3`, `python` (and `py -3` on Windows), or `node` |
| `node_args` | Extra Node.js flags |
| `timeout` | Seconds to wait for each call. A call that takes longer raises an error and stops that worker, since its late reply cannot be matched any more. Start a new bridge to continue |
| `start_timeout` | Seconds to wait for the worker to start (default 60) |
| `cwd` | Folder the worker runs in and loads modules from |
| `env` | Extra environment variables |
| `stderr` | `"inherit"` (default: shown in the terminal) or `"null"` |

## Errors

An exception in Python or JavaScript becomes a GlowLang error:

```glow
try:
    py.call("math.sqrt", [-1])
catch e:
    show e                     # python ValueError: math domain error (wording varies by Python version)
    show py.last_traceback     # the full Python traceback
end
```

These also raise errors you can catch: the interpreter is not found, the
worker crashes (`the python worker stopped (exit code 1)`), a call times out,
or the bridge is used after `close()`.

## Notes

- **Output:** anything the foreign code prints goes to standard error, so it
  shows in the terminal but can never corrupt the replies.
- **Threads:** a bridge handles one call at a time. Give each GlowLang thread
  its own bridge.
- **Speed:** each call crosses a process boundary. Batch work into one call
  rather than calling in a tight loop. For example, pass the whole list to
  one Python function instead of calling it once per item.
- **C libraries:** call them directly with [`ffi`](ffi_guide.md).
- **Other languages:** for languages without a bridge, run them with the
  [`process` module](process_guide.md).
