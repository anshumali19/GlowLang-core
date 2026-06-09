# GlowLang Debugger Guide

## GlowLang-Level Debugging (lib/debugger.glow)

Use the built-in debugger library for runtime introspection:

```glow
use "lib/debugger.glow"

# Assert with message
debug.assert(x > 0, "x must be positive")

# Get stack trace
trace = debug.stacktrace()
show trace

# Inspect heap
stats = debug.heap_stats()
show "Allocated: " + stats["allocated"]
show "Freed: " + stats["freed"]
```

### Available Functions

| Function | Description |
|----------|-------------|
| `debug.assert(cond, msg)` | Assert condition, throw on failure |
| `debug.stacktrace()` | Return current stack trace as string |
| `debug.heap_stats()` | Return GC heap statistics dict |
| `debug.break()` | Trigger a breakpoint (SIGINT) |
| `debug.profile_start()` | Start profiling |
| `debug.profile_end()` | End profiling and report |

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
