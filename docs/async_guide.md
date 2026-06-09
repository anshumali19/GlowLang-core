# GlowLang Async & Concurrency Guide

A comprehensive guide to writing concurrent and asynchronous programs in GlowLang — covering thread spawning, tasks, the GIL, synchronization patterns, and real-world examples.

---

## Concurrency Model Overview

GlowLang provides concurrency through **OS-level threads** with a **Global Interpreter Lock (GIL)**. This model offers:

- **Simple mental model** — no data races on GlowLang objects
- **True I/O concurrency** — the GIL is released during blocking I/O
- **Lightweight spawning** — create threads with a single `spawn` keyword

```
Main Thread ─────────────────────────────────────▶
                 │           │           │
              spawn       spawn       spawn
                 │           │           │
Worker 1 ────────┘           │           │
Worker 2 ────────────────────┘           │
Worker 3 ────────────────────────────────┘
```

---

## 1. Basic Spawning

### The `spawn` Keyword
Use `spawn` to launch a function in a new OS thread:

```glow
func greet(name):
    show f"Hello from thread: {name}"
end

spawn greet("Worker-A")
spawn greet("Worker-B")

show "Main thread continues immediately"
os.sleep(100)   # Wait for workers to finish
```

**Output (order may vary):**
```
Main thread continues immediately
Hello from thread: Worker-A
Hello from thread: Worker-B
```

### Spawning with Arguments
```glow
func compute(id, iterations):
    let sum = 0
    let i = 0
    while i < iterations:
        sum = sum + i
        i = i + 1
    end
    show f"Worker {id}: sum = {sum}"
end

spawn compute(1, 1000)
spawn compute(2, 2000)
spawn compute(3, 3000)

os.sleep(2000)
show "All workers complete"
```

---

## 2. Task Declaration

### The `task` Keyword
Use `task` to declare functions that are designed for concurrent execution:

```glow
task fetch_page(url):
    show f"Fetching {url}..."
    os.sleep(500)   # Simulate network I/O
    show f"Fetched {url} successfully"
    return "HTML content"
end

task process_data(data):
    show "Processing data..."
    os.sleep(300)   # Simulate computation
    show "Data processed"
end

# Launch tasks concurrently
spawn fetch_page("https://example.com")
spawn fetch_page("https://api.example.com")
spawn process_data("raw-data")

os.sleep(1000)
```

`task` is semantically identical to `func` — it's a documentation convention indicating the function is designed for concurrent use.

---

## 3. Understanding the GIL

### How the GIL Works

The Global Interpreter Lock ensures only one thread executes GlowLang bytecode at a time:

```
Time ────────────────────────────────────────────▶

Thread 1:  [CPU work] ─── [I/O: release GIL] ─── [CPU work]
Thread 2:  [waiting]  ─── [CPU work] ──────────── [waiting]
Thread 3:  [waiting]  ─── [waiting]  ──────────── [CPU work]
```

### When the GIL is Released
The GIL is automatically released during native I/O operations:

| Operation | GIL Released? | True Concurrency? |
|-----------|:-------------:|:-----------------:|
| `os.sleep()` | ✅ | ✅ |
| `http.accept()` | ✅ | ✅ |
| `http.read()` | ✅ | ✅ |
| `http.send()` | ✅ | ✅ |
| `sqlite.query()` | ✅ | ✅ |
| `sqlite.exec()` | ✅ | ✅ |
| `redis.get()` | ✅ | ✅ |
| `fs.read()` | ✅ | ✅ |
| Pure computation | ❌ | ❌ |
| `tensor.add()` | ✅ | ✅ (SIMD native) |

### Implication
- **I/O-bound workloads** achieve true parallelism
- **CPU-bound workloads** are serialized (one thread at a time)
- **Tensor operations** run outside the GIL for maximum SIMD throughput

---

## 4. Concurrent Server Pattern

### Multi-Client Web Server
```glow
func handle_client(client):
    let request = http.read(client)
    show "Received request"

    # Simulate processing
    os.sleep(100)

    let body = "{\"status\": \"ok\"}"
    let response = "HTTP/1.1 200 OK\r\n"
    response = response + "Content-Type: application/json\r\n"
    response = response + "Content-Length: " + len(body) + "\r\n\r\n"
    response = response + body

    http.send(client, response)
    http.close(client)
    show "Client served"
end

show "Starting concurrent server on port 8080..."
let server = http.listen(8080)

let count = 0
while count < 100:
    let client = http.accept(server)    # GIL released here
    spawn handle_client(client)         # Handle in new thread
    count = count + 1
end

http.close(server)
```

Each client connection is handled in its own thread, and `http.accept()` releases the GIL so other threads can run.

---

## 5. Worker Pool Pattern

### Fan-Out / Fan-In
```glow
let results = []

func worker(id, data):
    show f"Worker {id} processing..."
    os.sleep(200)

    # Simulate work
    let result = len(data) * 2
    show f"Worker {id} done: {result}"
    append(results, result)
end

# Fan out: distribute work across workers
let datasets = ["data-a", "data-bb", "data-ccc", "data-dddd"]
let i = 0
for each data in datasets:
    spawn worker(i, data)
    i = i + 1
end

# Fan in: wait for all results
os.sleep(1000)
show "All results:", results
```

---

## 6. Periodic Task Pattern

### Background Timer
```glow
func health_check():
    let count = 0
    while count < 5:
        show f"Health check #{count}: OK"
        os.sleep(2000)   # Check every 2 seconds
        count = count + 1
    end
    show "Health check monitor stopped"
end

func main_work():
    show "Main application running..."
    os.sleep(10000)
    show "Main application done"
end

# Launch health check in background
spawn health_check()

# Main work continues
main_work()
```

---

## 7. Concurrent I/O Pattern

### Parallel Data Fetching
```glow
task fetch_users():
    show "Fetching users from database..."
    let db = sqlite.open("app.db")
    let users = sqlite.query(db, "SELECT * FROM users")
    sqlite.close(db)
    show "Users fetched:", len(users), "records"
end

task fetch_config():
    show "Fetching config from Redis..."
    let r = redis.connect("127.0.0.1", 6379)
    when r is not none:
        let config = redis.get(r, "app_config")
        show "Config fetched:", config
    otherwise:
        show "Redis unavailable, using defaults"
    end
end

task send_notification():
    show "Sending notification..."
    os.sleep(300)   # Simulate HTTP call
    show "Notification sent"
end

# Launch all I/O operations concurrently
spawn fetch_users()
spawn fetch_config()
spawn send_notification()

show "All I/O tasks launched"
os.sleep(2000)
show "Application ready"
```

---

## 8. Error Handling in Concurrent Code

### Thread-Safe Exception Handling
Each spawned thread has its own exception handler chain:

```glow
func risky_worker(id):
    try:
        show f"Worker {id} starting"

        when id == 2:
            throw f"Worker {id} encountered an error!"
        end

        show f"Worker {id} completed successfully"
    catch e:
        show f"Worker {id} error: {e}"
    end
end

spawn risky_worker(1)
spawn risky_worker(2)
spawn risky_worker(3)

os.sleep(500)
show "Main thread: all workers have been handled"
```

### Defensive Spawning Pattern
```glow
func safe_spawn(task_fn, task_name):
    func wrapper():
        try:
            task_fn()
        catch e:
            show f"[ERROR] Task '{task_name}' failed: {e}"
        end
    end
    spawn wrapper()
end

func dangerous_work():
    throw "Unexpected failure!"
end

safe_spawn(dangerous_work, "data-sync")
os.sleep(500)
```

---

## 9. Concurrency with Classes

### Thread-Safe Service
```glow
blueprint WorkerPool:
    action init(size):
        self.size = size
        self.active = 0
    end

    action submit(task_fn, task_id):
        show f"Submitting task {task_id}"
        self.active = self.active + 1
        spawn task_fn(task_id)
    end

    action status():
        show f"Pool: {self.active} tasks submitted (pool size: {self.size})"
    end
end

func do_work(id):
    show f"Task {id} executing..."
    os.sleep(500)
    show f"Task {id} complete"
end

let pool = WorkerPool(4)
pool.init(4)
pool.submit(do_work, 1)
pool.submit(do_work, 2)
pool.submit(do_work, 3)
pool.status()

os.sleep(2000)
```

---

## 10. Best Practices

### Do's
- ✅ Use `spawn` for I/O-bound tasks (HTTP, database, file I/O)
- ✅ Use `try/catch` inside spawned functions for error isolation
- ✅ Use `os.sleep()` to wait for spawned threads when needed
- ✅ Keep shared state minimal between threads
- ✅ Use `task` keyword to document concurrent-intended functions

### Don'ts
- ❌ Don't rely on thread execution order
- ❌ Don't spawn hundreds of threads for CPU-bound work (GIL limits benefit)
- ❌ Don't modify shared lists from multiple threads without coordination
- ❌ Don't forget to wait for spawned threads before program exit

### Performance Tips
- For CPU-bound parallelism, use `tensor.*` operations (they release the GIL and use SIMD)
- For I/O parallelism, spawn freely — the GIL is released during all I/O
- Profile with `os.time()` to measure actual concurrency gains

---

*GlowLang Async & Concurrency Guide v1.0*
