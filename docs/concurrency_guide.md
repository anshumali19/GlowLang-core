# GlowLang Concurrency & Async Guide

## Overview

GlowLang provides concurrency through the `spawn` keyword, which creates OS-level threads
protected by a Global Interpreter Lock (GIL). This guide covers verified concurrency patterns.

## Spawning Threads

```
func worker(name):
    show "Worker " + name + " started"
    # ... do work ...
    show "Worker " + name + " done"
end

spawn worker("A")
spawn worker("B")
```

### Important: GIL Semantics
- GlowLang uses a GIL (Global Interpreter Lock) for thread safety
- Only one thread executes GlowLang bytecode at a time
- The GIL is released during blocking I/O operations (HTTP, SQLite, sleep)
- CPU-bound parallelism is NOT achieved — use spawn for I/O-bound concurrency

## Thread Communication

### Shared Mutable State (via Lists)
```
let shared = [0]  # Use a list as a mutable shared cell

func producer():
    let i = 0
    while i < 10:
        shared[0] = shared[0] + 1
        os.sleep(100)
        i = i + 1
    end
end

spawn producer()
os.sleep(1500)
show "Counter: " + shared[0]
```

### Message Passing (via Shared List)
```
let messages = []

func sender():
    append(messages, "hello")
    append(messages, "world")
end

func receiver():
    os.sleep(200)
    for each msg in messages:
        show "Received: " + msg
    end
end

spawn sender()
spawn receiver()
os.sleep(500)
```

## Reactive Programming

### The `whenever` Pattern
```
let sensor_value = 0

whenever sensor_value > 100:
    show "ALERT: Sensor value exceeded 100!"
end
```

Note: `whenever` is evaluated at parse-time as a conditional. For true reactive
behavior, implement polling loops:

```
func monitor(data, threshold):
    while true:
        when data[0] > threshold:
            show "Threshold exceeded: " + data[0]
            return
        end
        os.sleep(100)
    end
end

let data = [0]
spawn monitor(data, 50)
```

## Timer Patterns

```
func delayed_action(delay_ms, action_name):
    os.sleep(delay_ms)
    show "Executing: " + action_name
end

spawn delayed_action(2000, "send_report")
spawn delayed_action(5000, "cleanup")
```

## Best Practices

1. **Avoid shared mutable state** — prefer message passing via shared lists
2. **Use os.sleep() for coordination** — the GIL is released during sleep
3. **Keep critical sections short** — minimize time between reads and writes
4. **Use lists as mutable cells** — `let counter = [0]` allows cross-thread mutation
5. **Graceful shutdown** — use a shared flag to signal threads to stop
6. **Timeout patterns** — always include timeouts in wait loops

## Known Limitations

- No built-in mutex/semaphore primitives (GIL provides basic safety)
- No thread join mechanism (use shared flags + polling)
- No thread cancellation (threads must check for stop flags)
- CPU-bound parallelism limited by GIL
- Thread-local storage not available
