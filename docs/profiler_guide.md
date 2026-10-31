# GlowLang Profiler Guide

## GlowLang-Level Profiling (lib/profiler.glow)

```glow
use "lib/profiler.glow"

profiler.start()

# Code to profile
every i in from 1 to 1000:
    x = i * i
end

profiler.stop()
profiler.report()
```

### Available Functions

| Function | Description |
|----------|-------------|
| `profiler.start()` | Begin profiling |
| `profiler.stop()` | Stop profiling |
| `profiler.report()` | Print profiling report |
| `profiler.time(name, func)` | Time a function call |
| `profiler.memory()` | Get current memory usage |

## Line Profiling from the Command Line

`glowlang profile` runs a program and then lists the lines that ran most often:

```bash
glowlang profile program.glow
```

```
--- Performance Hot-Spots ---
  Executions | Source Location
  -----------+------------------
       46401 | program.glow:5
       27900 | program.glow:2
```

Profiling runs the program in the interpreter (compiled code does not count
lines), so it is slower than `glowlang run`. In VS Code, **GlowLang: Profile
Current File** runs the same command.

## Trace Mode

`glowlang trace` prints every bytecode instruction as it runs, with the
registers after it:

```bash
glowlang trace program.glow > trace.log
```

`tools/debugger/glow_debugger.c` replays such a log (see the debugger guide).

## Tools Not Connected Yet

`tools/profiler/glow_profiler.c`, `tools/inspector/glow_inspector.c`,
`tools/profiler/profiler.html` and `tools/stack_visualizer/visualizer.html`
read trace formats (`[TRACE]`/`[GC]` lines, JSON profiles) that the runtime
does not produce, so they show nothing, or sample data, today. Use
`glowlang profile` and `glowlang trace` instead.
