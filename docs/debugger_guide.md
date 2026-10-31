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

## Standalone Debugger (tools/debugger/glow_debugger.c)

A GDB-style debugger for inspecting running GlowLang processes:

```bash
# Build
gcc tools/debugger/glow_debugger.c -o bin/glow_debugger

# Attach to a running process
./bin/glow_debugger --pid <PID>

# Debug a program
./bin/glow_debugger program.glow
```

### Commands

| Command | Description |
|---------|-------------|
| `break <line>` | Set breakpoint at line |
| `continue` | Continue execution |
| `step` | Step one instruction |
| `print <var>` | Print variable value |
| `backtrace` | Show call stack |
| `locals` | Show local variables |
| `heap` | Show heap stats |
| `watch <var>` | Watch variable for changes |
| `quit` | Exit debugger |
