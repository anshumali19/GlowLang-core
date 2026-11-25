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

`glowlang profile` runs a program and then lists the lines that ran most often
and the time each function took:

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

```
--- Time by Function ---
       Calls |    Self ms |   Total ms | Function
  -----------+------------+------------+---------
        8361 |       0.55 |       0.55 | fib
           1 |       0.34 |       1.13 | <main>
         200 |       0.15 |       0.15 | re_findall
           1 |       0.05 |       0.77 | work
         200 |       0.02 |       0.17 | words
```

The second table lists the 15 functions that took the most time of their
own. **Self** is the time spent in the function itself, without the GlowLang
functions it calls; time in built-in calls (a regular expression, a file
read) counts as the caller's own. **Total** includes what it calls, and a
recursive function's time is counted once, not once per level. `<main>` is
the program's top-level code. The table is left out when no function was
called; `debug.profile_start()` and `debug.profile_stop()` print the same
report for just the code between them.

Profiling runs the program in the interpreter (compiled code does not count
lines), so it is slower than `glowlang run`, and the times are best compared
with each other, not with a normal run. In VS Code, **GlowLang: Profile
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
