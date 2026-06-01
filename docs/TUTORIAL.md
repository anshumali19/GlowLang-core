# GlowLang Tutorial

Welcome to GlowLang! This guide covers the core syntax and unique features of the language.

## 1. Basics
GlowLang is dynamically typed but relies on strict underlying memory structures.

```glow
# Variables and Output
let name = "Developer"
let age = 30
show "Hello, " + name + "! You are " + age
```

## 2. Control Flow
GlowLang eschews `if/else` for `when/eif/otherwise`, reading more naturally.

```glow
let temperature = 75

when temperature > 80:
    show "It's hot!"
eif temperature < 60:
    show "It's cold!"
otherwise:
    show "It's perfect."
end
```

## 3. Loops
GlowLang optimizes loops aggressively. The `every` keyword is JIT-analyzed for SIMD Auto-Vectorization.

```glow
# Standard loop
let count = 0
while count < 5:
    show count
    count = count + 1
end

# Range loop
every i from 1 to 10:
    show i
end
```

## 4. Functions (Actions)
Functions are defined using `func` or `action`.

```glow
action greet(user):
    return "Hello, " + user
end

show greet("Alice")
```

## 5. Async & Concurrency
GlowLang supports lightweight thread spawning for concurrent tasks.

```glow
action fetch_data(id):
    os.sleep(500) # Yields the GIL safely
    show "Data " + id + " fetched!"
end

spawn fetch_data(1)
spawn fetch_data(2)
```

## 6. SIMD Arrays
For high-performance math (like AI/ML), use `packed_float` arrays.

```glow
let size = 1000
let a = packed_float(size)
let b = packed_float(size)
let res = packed_float(size)

# The JIT will Auto-Vectorize this loop using AVX2/SSE!
every i from 0 to size - 1:
    res[i] = a[i] + b[i]
end
```

## 7. Error Handling & Debugging
Exceptions are thrown and caught naturally. If uncaught, GlowLang provides a full JIT-aware stack trace.

```glow
action divide(a, b):
    when b == 0:
        throw "Cannot divide by zero!"
    end
    return a / b
end

try:
    show divide(10, 0)
catch err:
    show "Caught error: ", err
    # Print the exact call stack for analysis:
    debug.stacktrace()
end
```
