# GlowLang Debugger Guide

## GlowLang-Level Debugging (lib/debugger.glow)

Use the built-in debugger library for runtime introspection:

```glow
use "lib/debugger.glow"

let x = 5
debug.assert(x > 0, "x must be positive")   # raises when the condition is false

debug.stacktrace()          # prints the current call stack

let stats = debug.heap_stats()
show "Allocated: " + str(stats["allocated"]) + " bytes"
show "Next collection at: " + str(stats["threshold"]) + " bytes"
```

### Available Functions

| Function | Description |
|----------|-------------|
| `debug.assert(cond, msg)` | Raise an error with msg when cond is false |
| `debug.stacktrace()` | Print the current call stack |
| `debug.dump_stack()` | Print the current frame's registers |
| `debug.heap_stats()` | GC heap statistics: `allocated` and `threshold` (bytes) |
| `debug.breakpoint()` | Print a breakpoint marker and the current frame (it does not pause the program; for that, run `glowlang debug program.glow`) |
| `debug.profile_start()` | Start counting how often each line runs |
| `debug.profile_end()` / `debug.profile_stop()` | Stop and print the most-run lines |

`debug.heap_stats`, `debug.breakpoint` and `debug.profile_end` come from
`lib/debugger.glow` (also available as `debug_heap_stats()` and so on); the
others are built in.

## Interactive Debugger

`glowlang debug program.glow` stops before each instruction and waits for a
command at the `(glowdb)` prompt.

## Trace Replay (tools/debugger/glow_debugger.c)

A standalone tool that replays a trace recorded with `glowlang trace`; it does
not attach to running processes.

```bash
# Build
gcc tools/debugger/glow_debugger.c -o bin/glow_debugger

# Record, then replay
glowlang trace program.glow > trace.log
./bin/glow_debugger --trace trace.log --interactive
```

### Commands

| Command | Description |
|---------|-------------|
| `break <line>` | Set breakpoint at line |
| `continue` | Continue execution |
| `step` | Step one instruction |
| `print <reg>` | Print a register's value from the trace |
| `backtrace` | Show call stack |
| `locals` | Show local variables |
| `heap` | Show heap stats |
| `watch <reg>` | Stop when a register changes |
| `quit` | Exit debugger |
