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

## Trace Mode

Run with `--trace` to generate detailed execution traces:

```bash
glowlang --trace program.glow 2>trace.log
```

## Standalone Profiler (tools/profiler/glow_profiler.c)

```bash
# Build
gcc tools/profiler/glow_profiler.c -o bin/glow_profiler

# Text report
./bin/glow_profiler trace.log

# HTML report
./bin/glow_profiler --html trace.log > report.html

# Flamegraph data
./bin/glow_profiler --flamegraph trace.log > stacks.folded
# Use with Brendan Gregg's flamegraph.pl:
# flamegraph.pl stacks.folded > flamegraph.svg
```

## Inspector (tools/inspector/glow_inspector.c)

```bash
# Build
gcc tools/inspector/glow_inspector.c -o bin/glow_inspector

# Full report
./bin/glow_inspector trace.log

# GC stats only
./bin/glow_inspector --gc trace.log

# Stack only
./bin/glow_inspector --stack trace.log
```
