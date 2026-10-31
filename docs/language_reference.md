# GlowLang Language Reference

Complete reference for the GlowLang programming language — covering every syntactic construct, keyword, type, operator, and standard library module.

---

## Table of Contents

1. [Lexical Structure](#1-lexical-structure)
2. [Types and Values](#2-types-and-values)
3. [Variables and Assignment](#3-variables-and-assignment)
4. [Operators](#4-operators)
5. [Strings](#5-strings)
6. [Lists](#6-lists)
7. [Dictionaries](#7-dictionaries)
8. [Control Flow](#8-control-flow)
9. [Functions](#9-functions)
10. [Lambdas and Closures](#10-lambdas-and-closures)
11. [Classes and Objects](#11-classes-and-objects)
12. [Traits](#12-traits)
13. [Error Handling](#13-error-handling)
14. [Modules and Imports](#14-modules-and-imports)
15. [Concurrency](#15-concurrency)
16. [Reactive Programming](#16-reactive-programming)
17. [List Comprehensions](#17-list-comprehensions)
18. [Standard Library](#18-standard-library)
19. [Keywords Reference](#19-keywords-reference)

---

## 1. Lexical Structure

### Comments
```glow
# Single-line comment

/* Multi-line
   comment */
```

### Identifiers
Identifiers begin with a letter or underscore, followed by letters, digits, or underscores. GlowLang is case-sensitive.

### Statement Termination
Statements are terminated by newlines. No semicolons required. Inside open brackets `( [ {`,
newlines do not end the statement, so long literals and calls can span lines:

```glow
let people = [
    {"name": "Ada", "age": 36},
    {"name": "Alan", "age": 41}
]
```

### Blocks
All block constructs end with the `end` keyword:
```glow
if condition:
    # body
end
```

---

## 2. Types and Values

GlowLang is dynamically typed with the following value types:

| Type | Example | Description |
|------|---------|-------------|
| Integer | `42`, `-7`, `2 ** 100` | Exact integer of any size (never overflows) |
| Float | `3.14`, `-0.5`, `1e-3` | 64-bit double-precision |
| String | `"hello"`, `f"hi {x}"` | Byte string (UTF-8 text) |
| Boolean | `true`, `false` | Boolean value |
| List | `[1, 2, 3]` | Dynamic array |
| Dictionary | `{"a": 1}` | Hash map |
| None | `none` | Null/absent value |
| Tensor | `tensor.zeros(10)` | SIMD-accelerated numeric array |
| Lambda | `lambda(x): x * 2` | Anonymous function |
| Object | `MyClass()` | Class instance |

### Type Checking
```glow
let t = type(value)   # Returns type as string: "int", "float", "string", "list", "dict", etc.
```

Conversions: `int(x)`, `float(x)`, `str(x)` (also `string(x)`), `bool(x)`.

---

## 3. Variables and Assignment

### Declaration
```glow
let x = 10           # Declare and initialize
let name = "Glow"    # String variable
```

### Reassignment
```glow
x = x + 1            # Reassign existing variable
x += 5               # Compound assignment
```

### Compound Assignment Operators
```glow
x += 1    # x = x + 1
x -= 2    # x = x - 2
x *= 3    # x = x * 3
x /= 4    # x = x / 4
```

---

## 4. Operators

### Arithmetic
| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition (or joining text / lists) | `3 + 4` → `7`, `"a" + 1` → `"a1"` |
| `-` | Subtraction | `10 - 3` → `7` |
| `*` | Multiplication (or repeating text / lists) | `5 * 6` → `30`, `"ab" * 2` → `"abab"` |
| `/` | Division | `15 / 4` → `3.75` |
| `%` | Modulo (sign of the divisor) | `17 % 5` → `2`, `-7 % 3` → `2` |
| `**` | Power (exact for integers) | `2 ** 10` → `1024`, `2 ** -1` → `0.5` |

Arithmetic on values that are not numbers raises an error (`none * 2` →
"Cannot apply '*' to none and int"). Division and modulo by zero raise errors.

### Comparison
| Operator | Description | Example |
|----------|-------------|---------|
| `==` | Equal (lists and dicts compare by contents) | `[1, 2] == [1, 2]` → `true` |
| `!=` | Not equal | `x != 5` |
| `<` `>` `<=` `>=` | Ordering of numbers, text (by character codes) and lists | `"apple" < "banana"` |

Ordering values of different kinds (`"a" < 1`) raises an error.

### Membership
| Operator | Example |
|----------|---------|
| `in`, `is in` | `2 in [1, 2, 3]`, `"x" is in letters` |
| `not in`, `is not in` | `5 not in [1, 2, 3]` |

### Logical
| Operator | Description | Example |
|----------|-------------|---------|
| `and` | Logical AND | `a and b` |
| `or` | Logical OR | `a or b` |
| `not` | Logical NOT | `not a` |

### Identity
| Operator | Description | Example |
|----------|-------------|---------|
| `is` | Same as `==` | `x is true` |
| `is not` | Same as `!=` | `x is not none` |

### String Concatenation
```glow
let full = "Hello" + " " + "World"   # "Hello World"
```

---

## 5. Strings

### String Literals
```glow
let s1 = "Hello, World!"       # Double-quoted string
let s2 = "Line1\nLine2"        # Escape sequences: \n, \t, \r, \\, \"
let s3 = '{"ok": true}'        # Single quotes: handy for JSON
```

### F-Strings (Interpolation)
Any expression can go between braces; `{{` and `}}` are literal braces.
```glow
let name = "GlowLang"
let version = 1
show f"Welcome to {name} v{version}!"
show f"{name.upper()} has {len(name)} letters, next version {version + 1}"
```

### String Operations
```glow
let s = "Hello, World"
let length = len(s)          # String length
let ch = s[0]                # Character by index (s[-1] is the last); out of range raises
let sub = s[0:5]             # Slicing: "Hello"
let parts = s.split(", ")    # Split into a list
```

### String Methods
| Method | Result |
|--------|--------|
| `s.upper()`, `s.lower()` | Changed case |
| `s.trim()` (`strip`), `s.trim_start()`, `s.trim_end()` | Whitespace removed |
| `s.replace(old, new)` | Every occurrence replaced |
| `s.contains(x)`, `s.starts_with(x)`, `s.ends_with(x)` | `true` / `false` |
| `s.find(x)` | Position of `x`, or `-1` |
| `s.count(x)` | Number of occurrences |
| `s.split(sep)`, `s.lines()` | List of parts |
| `sep.join(list)` | Items joined with `sep` |
| `s.is_number()` | Whether `float(s)` would succeed |

---

## 6. Lists

### Creation
```glow
let empty = []
let nums = [1, 2, 3, 4, 5]
let mixed = [1, "two", 3.0, true]
```

### Access and Modification
```glow
let first = nums[0]          # Index access (0-based)
let last = nums[len(nums) - 1]
nums[0] = 100                # Index assignment

# Negative indexing
let tail = nums[-1]           # Last element
```

Indexing past either end raises "List index out of range". Looking up a missing
dictionary key gives `none` (or use `d.get(key, default)`).

### Slicing
```glow
let sub = nums[1:3]          # Elements at index 1, 2
let from2 = nums[2:]         # From index 2 to end
let to3 = nums[:3]           # From start to index 2
```

### List Operations
```glow
nums.push(6)                 # Append (also append(nums, 6))
let length = len(nums)       # Length
let r = range(10)            # [0, 1, 2, ..., 9]
```

### List Methods
| Method | Result |
|--------|--------|
| `push(x)` / `append(x)`, `pop()`, `insert(i, x)`, `extend(list)` | Change the list |
| `remove(x)` | Removes the first `x`; `true` if one was removed |
| `index(x)`, `contains(x)` | Position (or `-1`), membership |
| `sort(key, descending)`, `reverse()` | Reorder in place |
| `map(f)`, `filter(f)`, `reduce(f, start)`, `each(f)` | Functional helpers |
| `join(sep)`, `copy()`, `clear()`, `first()`, `last()`, `is_empty()` | |

Global helpers: `sorted(list, key, descending)`, `reversed(x)`, `min(...)`, `max(...)`,
`sum(list)`, `abs(x)`.

```glow
let scores = [72, 95, 88]
show sorted(scores, none, true)                 # [95, 88, 72]
show scores.map(lambda(x): x + 5)               # [77, 100, 93]
show scores.filter(lambda(x): x > 80).join(", ")
```

---

## 7. Dictionaries

### Creation
```glow
let empty = {}
let user = {"name": "Alice", "age": 30}
```

### Access and Modification
```glow
let name = user["name"]           # Key access
user["email"] = "alice@glow.dev"  # Add/update key
```

### Dictionary Iteration
```glow
for each key, value in user:
    show key, ":", value
end

for each i, item in ["a", "b"]:     # index and item for lists
    show i, item
end
```

### Dictionary Methods
```glow
let has = user.has("name")              # Key exists?
let city = user.get("city", "unknown")  # Value or a default
let names = user.keys()
let entries = user.items()              # [[key, value], ...]
user.remove("email")
```

---

## 8. Control Flow

### If / Else
```glow
if x > 0:
    show "Positive"
else:
    show "Non-positive"
end
```

### When / Eif / Otherwise (Natural Style)
```glow
when temperature > 80:
    show "Hot"
eif temperature > 60:
    show "Warm"
eif temperature > 40:
    show "Cool"
otherwise:
    show "Cold"
end
```

### Conditional Expression
`value when condition otherwise other` picks one of two values. Only the
chosen side is evaluated. It binds more loosely than every other operator, and
can be chained:

```glow
let kind = "even" when n % 2 == 0 otherwise "odd"
let size = "big" when n > 100 otherwise "medium" when n > 10 otherwise "small"

func absval(v):
    return v when v >= 0 otherwise -v
end
```

### While Loop
```glow
let count = 0
while count < 10:
    show count
    count = count + 1
end
```

### For Each Loop
```glow
let items = ["apple", "banana", "cherry"]
for each item in items:
    show item
end
```

### Every Loop (Range-based)
```glow
every i from 0 to 9:
    show i
end

# Iterating over collections
every row in rows:
    show row
end
```

---

## 9. Functions

### Function Declaration
GlowLang provides three synonymous keywords for functions:

```glow
# Using 'func'
func add(a, b):
    return a + b
end

# Using 'action'
action greet(name):
    return "Hello, " + name
end

# Using 'can'
can process(data):
    show "Processing:", data
end
```

All three are semantically identical. Convention:
- `func` — general-purpose functions
- `action` — methods within classes/blueprints
- `can` — capability methods in blueprints

### Return Values
```glow
func square(n):
    return n * n
end

let result = square(5)   # 25
```

### Default Returns
Functions without an explicit `return` statement return `none`.

### Default Parameter Values
A parameter can have a default value, used when the caller leaves that
argument out (or passes `none`). A parameter with no default that the caller
leaves out is `none`.

```glow
func greet(name, greeting = "Hello", mark = "!"):
    return greeting + ", " + name + mark
end

greet("Ana")              # Hello, Ana!
greet("Ana", "Hi")        # Hi, Ana!
greet("Ana", none, ".")   # Hello, Ana.

let add = lambda(x, y = 10): x + y
add(1)                    # 11
```

The default is evaluated each time the function is called, so `items = []`
gives every call its own new list.

### Recursive Functions
```glow
func fibonacci(n):
    if n <= 1:
        return n
    end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

show fibonacci(10)   # 55
```

---

## 10. Lambdas and Closures

### Lambda Expressions
```glow
let double = lambda(x): x * 2
show double(5)    # 10

let add = lambda(x, y): x + y
show add(3, 4)    # 7
```

### Closures
Lambdas and functions defined inside other functions see the enclosing
function's variables. Each closure keeps its own copy of them, taken when it is
created; assigning to one updates that copy, so the value persists across calls:

```glow
func make_counter():
    let count = 0
    func next():
        count = count + 1
        return count
    end
    return next
end

let counter = make_counter()
show counter()   # 1
show counter()   # 2
```

The enclosing function does not see the closure's changes, and a variable
declared with `let` inside the closure is a new local.

### Lambdas in Collections
```glow
let ops = [lambda(x): x * 2, lambda(x): x * x]
show ops[0](5)   # 10
show ops[1](5)   # 25
```

### Higher-Order Functions
```glow
func apply(fn, list):
    let result = []
    for each item in list:
        append(result, fn(item))
    end
    return result
end

let squared = apply(lambda(x): x * x, [1, 2, 3, 4])
show squared   # [1, 4, 9, 16]
```

---

## 11. Classes and Objects

### Class Declaration
GlowLang provides two synonymous keywords — `class` and `blueprint`:

```glow
class Person:
    func init(name, age):
        self.name = name
        self.age = age
    end

    func greet():
        show f"Hi, I'm {self.name}, age {self.age}"
    end
end

let p = Person("Alice", 30)
p.greet()
```

### Blueprint Style
```glow
blueprint Server:
    action init(port):
        self.port = port
        self.running = false
    end

    action start():
        self.running = true
        show f"Server running on port {self.port}"
    end
end
```

### Self Reference
Inside methods, `self` refers to the current instance:
```glow
class Counter:
    action init():
        self.count = 0
    end

    action increment():
        self.count = self.count + 1
    end

    action value():
        return self.count
    end
end
```

### Property Access
```glow
let c = Counter()
c.init()
c.increment()
show c.count          # Direct property access
show c.value()        # Method access
```

---

## 12. Traits

Traits define reusable behavior that can be mixed into classes:

```glow
trait Drawable:
    func draw():
        show "Drawing..."
    end
end

trait Resizable:
    func resize(w, h):
        self.width = w
        self.height = h
    end
end

class Widget:
    use Drawable
    use Resizable

    func init(name):
        self.name = name
        self.width = 100
        self.height = 50
    end
end

let w = Widget("button")
w.init("button")
w.draw()             # From Drawable trait
w.resize(200, 100)   # From Resizable trait
```

---

## 13. Error Handling

### Try / Catch
```glow
try:
    show "Risky operation"
    throw "Something went wrong"
catch e:
    show "Caught:", e
end
```

### Throw
```glow
func divide(a, b):
    when b == 0:
        throw "Division by zero!"
    end
    return a / b
end
```

### Nested Try/Catch
```glow
try:
    try:
        throw "Inner error"
    catch e1:
        show "Caught inner:", e1
        throw "Propagated"
    end
catch e2:
    show "Caught outer:", e2
end
```

### Exception in Functions
```glow
func risky():
    throw "Error from function"
end

try:
    risky()
catch e:
    show "Caught:", e
end
```

---

## 14. Modules and Imports

### Importing Files
```glow
import "lib/utils.glow"         # Import local file
import "lib/ai.glow"            # Import AI library
```

### Remote Imports (via gdu (Glow Dependency Utility))
```glow
import "https://github.com/user/math.glow"
```

The `gdu (Glow Dependency Utility)` package manager resolves remote URLs to locally cached files.

### Module Pattern
```glow
# math_utils.glow
func square(n):
    return n * n
end

func cube(n):
    return n * n * n
end
```

```glow
# main.glow
import "math_utils.glow"

show square(5)   # 25
show cube(3)     # 27
```

---

## 15. Concurrency

### Spawning Threads
```glow
func worker(id):
    show f"Worker {id} started"
    os.sleep(500)
    show f"Worker {id} done"
end

spawn worker(1)
spawn worker(2)
spawn worker(3)

show "Main thread continues..."
os.sleep(1000)
```

### Tasks, Channels and Timers
`lib/async.glow` provides `Channel`, `Task`, `TaskGroup`, `Timer` and `Periodic`:

```glow
use "lib/async.glow"

let results = Channel("results")
func fetch(n):
    results.send(n * n)
end

let group = TaskGroup("work")
group.create("a", lambda(): fetch(2))
group.create("b", lambda(): fetch(3))
group.start_all()                  # run on threads and wait
show results.drain()
```

### Concurrency Model
GlowLang uses OS-level threads with a Global Interpreter Lock (GIL): one thread runs GlowLang code at a time. The GIL is released during `os.sleep`, `os.system` and HTTP/TLS socket calls, so threads overlap waiting, not computation. `threads.join_all()` waits for all spawned threads; the program also waits for them before exiting.

---

## 16. Reactive Programming

### Whenever Blocks
The `whenever` construct monitors a condition and fires reactively:

```glow
let temperature = 70

whenever temperature > 100:
    show "ALERT: Overheating!"
    # Take corrective action
end
```

### Event-Driven Patterns
```glow
let event_queue = []

whenever len(event_queue) > 0:
    let event = event_queue[0]
    show "Processing event:", event
end
```

---

## 17. List Comprehensions

### Basic Comprehension
```glow
let nums = [1, 2, 3, 4, 5]
let doubled = [x * 2 for each x in nums]
show doubled   # [2, 4, 6, 8, 10]
```

### Filtered Comprehension
```glow
let evens = [x for each x in nums when x % 2 == 0]
show evens   # [2, 4]
```

### Range Comprehension
```glow
let squares = [x * x for each x in range(10)]
show squares   # [0, 1, 4, 9, 16, 25, 36, 49, 64, 81]
```

### String Comprehension
```glow
let chars = [c for each c in "hello"]
show chars   # ["h", "e", "l", "l", "o"]
```

---

## 18. Standard Library

Every name below exists in the runtime (checked against `<module>.keys()`).

### Built-in Functions
| Function | Description |
|----------|-------------|
| `show a, b` / `print` | Print values separated by spaces |
| `len(x)` | Length of a string, list or dict |
| `type(x)` | `"int"`, `"float"`, `"string"`, `"bool"`, `"list"`, `"dict"`, `"none"`, class name, ... |
| `int(x)`, `float(x)`, `str(x)` / `string(x)`, `bool(x)` | Conversions; `int("42")`, `int("ff", 16)` parse text and raise on anything that is not a whole number |
| `range(n)` | `[0, 1, ..., n-1]` |
| `append(list, x)` | Add to a list |
| `pairs(x)` | `[index, item]` / `[key, value]` pairs |
| `abs`, `min`, `max`, `sum`, `round`, `floor`, `ceil` | Numbers (`min`/`max` take values or one list) |
| `sorted(list, key, descending)`, `reversed(x)` | New ordered list (or reversed string) |
| `input(prompt)` / `ask(prompt)` | Read a line from standard input (`none` at end of input) |
| `assert(condition, message)` | Raise when the condition is false |
| `dict()` | Empty dictionary |

### Math (`math.`)
`sqrt`, `pow`, `exp`, `log(x, base)` (an error for x <= 0), `log2`, `log10`, `sin`, `cos`, `tan`,
`asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `hypot`, `abs`, `min`, `max`, `floor`,
`ceil`, `round` (halves away from zero), `trunc`, `isnan`, `isinf`, `vec2_add`, `vec2_dist`, and
the constants `pi`, `e`, `tau`, `inf`, `nan`. NaN follows IEEE 754: `math.nan == math.nan` is
false, and every ordering comparison with NaN is false.

### Tensors (`tensor.`)
| Function | Description |
|----------|-------------|
| `zeros(n)`, `ones(n)`, `create(n)` | New tensor of n numbers |
| `get(t, i)`, `set(t, i, x)` | Element access |
| `add`, `sub`, `mul`, `dot` | Element-wise operations and dot product (AVX2/FMA when available) |
| `matmul(A, rA, cA, B, rB, cB)` | Matrix product |
| `transpose(A, rows, cols)`, `reshape(t, shape)`, `shape(t)` | Shape operations |
| `to_device(t, "cpu" / "cuda")` | Device placement (CUDA only in builds with nvcc) |

### HTTP (`http.`)
`listen(port)`, `accept(server)`, `read(client)`, `send(client, data)`, `close(handle)`,
`request(method, url, ...)`.

### SQLite (`sqlite.`)
`open(path)`, `exec(db, sql, params)` (returns the number of rows changed), `query(db, sql, params)`
(a list of row dicts), `step_query(db, sql, params)` (rows one at a time for `for each`),
`last_insert_id(db)`, `changes(db)`, `close(db)`.

Put values in SQL as placeholders and pass them in `params`: a list for `?`
(`sqlite.query(db, "SELECT * FROM users WHERE name = ?", [name])`) or a dict for `:name`.
They are bound, never parsed as SQL, so they cannot inject SQL. Never build SQL by joining
strings with values. Errors raise `sqlite: <message>`. 64-bit integers come back exact,
and BLOBs come back as byte strings. `exec` without `params` may run several statements
separated by `;`.

### Redis (`redis.`)
`connect(host, port)`, `get(conn, key)`, `set(conn, key, value)`.

### JSON (`json.`)
`parse(text)`, `stringify(value)`.

### Regular Expressions (`import "re"`)
```glow
import "re"
let m = re.find("(?<year>\d{4})-(\d\d)", "due 2026-10-31")
show m.text, m.start, m.stop, m.group(1), m.group("year")   # 2026-10 4 11 2026 2026
show re.findall("(\w+)=(\d+)", "x=1, y=2")          # [[x, 1], [y, 2]]
show re.replace("(\w+)@(\w+)", "ana@glow", "\2:\1")  # glow:ana
show re.replace("\d+", "3 apples", lambda(m): str(int(m.text) * 2))
show re.split("[,;] *", "a, b;c")                    # [a, b, c]
```
| Function | Result |
|----------|--------|
| `re.find(pattern, text)` | First match, or `none` |
| `re.fullmatch(pattern, text)` | A match if the whole text matches, or `none` |
| `re.findall(pattern, text)` | Every match's text (or its group, or a list of its groups) |
| `re.finditer(pattern, text)` | Every match as a match object |
| `re.replace(pattern, text, replacement, count)` / `re.sub` | `\1` and `\g<name>` insert groups; the replacement can also be a function of the match |
| `re.split(pattern, text, maxsplit)` | Pieces between matches; groups in the pattern are kept |
| `re.escape(text)` | Text with the metacharacters escaped |
| `re.match(pattern, text)` / `re.search(pattern, text)` | Whether it occurs / the first matching text |
| `re.compile(pattern)` | An object with the methods above |

A match has `.text`, `.start`, `.stop` (`end` is a keyword), `.span`,
`.groups`, `.named` and `.group(n)`. Patterns support groups `( )`, named
groups `(?<name> )`, `(?: )`, `|`, greedy and lazy `* + ? {m,n}`, sets with
ranges and `[:classes:]`, `\d \w \s \b` and their negations, `^ $`, and a
leading `(?i)` to ignore case. Matching takes time linear in the text, with
no catastrophic backtracking. Positions are byte offsets.

### OS (`os.`)
`time()`, `format_time(t)`, `sleep(ms)` (fractions allowed; releases the GIL), `system(cmd)`,
`env(name)` / `getenv(name)`, `exit(code)`, `gc_info()`.

### Files (`fs.`)
`read(path)`, `write(path, text)`, `exists(path)`, `remove(path)`, `mkdir(path)`, `list(dir)` /
`readdir(dir)`, `size(path)`, `stat(path)`.

### Crypto (`crypto.`)
`sha256(data)`, `hmac(key, data)` / `hmac_sha256` (HMAC-SHA256), `blake3(data)`: hex digests
of the whole string, including any zero bytes. `random_bytes(n)` and `random_hex(n)`: n bytes
from the operating system's secure random source, for keys and tokens. `equal(a, b)`: compare
secrets in constant time. `uuid4()`: a random UUID. For signing messages, see `lib/kavach.glow`.

### Threads (`threads.`)
`join_all()`, `active()`. See also `lib/async.glow` and `lib/sync.glow`.

### Debugging (`debug.`)
`assert(condition, message)`, `stacktrace()`, `dump_stack()`, `profile_start()`, `profile_stop()`.

### Other modules
`gui` (X11: `open`, `clear`, `draw_rect`, `poll`), `collision.aabb(...)`, `crypto`, `net`, `ssl`.

---

## 19. Keywords Reference

| Keyword | Category | Description |
|---------|----------|-------------|
| `let` | Variables | Declare a variable |
| `show` | Output | Print to stdout |
| `func` | Functions | Declare a function |
| `action` | Functions | Synonym for `func` |
| `can` | Functions | Synonym for `func` |
| `return` | Functions | Return from function |
| `class` | OOP | Declare a class |
| `blueprint` | OOP | Synonym for `class` |
| `self` | OOP | Current instance reference |
| `trait` | OOP | Declare a trait |
| `use` | OOP/Import | Mix in a trait / Import module |
| `import` | Modules | Import a file |
| `if` | Control | Conditional branch |
| `else` | Control | Else branch |
| `end` | Control | End block |
| `when` | Control | Natural conditional (synonym for `if`) |
| `eif` | Control | Else-if (natural style) |
| `otherwise` | Control | Else (natural style) |
| `while` | Loops | While loop |
| `for each` | Loops | For-each iteration |
| `every` | Loops | Range-based / collection loop |
| `in` | Loops | Iteration target |
| `from` | Loops | Range start |
| `to` | Loops | Range end |
| `lambda` | Functions | Anonymous function |
| `try` | Errors | Begin try block |
| `catch` | Errors | Handle exception |
| `throw` | Errors | Raise exception |
| `spawn` | Concurrency | Launch concurrent thread |
| `task` | Concurrency | Declare async task |
| `whenever` | Reactive | Reactive condition monitor |
| `true` | Literals | Boolean true |
| `false` | Literals | Boolean false |
| `none` | Literals | Null value |
| `and` | Logic | Logical AND |
| `or` | Logic | Logical OR |
| `not` | Logic | Logical NOT |
| `is` | Logic | Identity comparison |
| `is not` | Logic | Negative identity |

---

*GlowLang Language Reference v1.0*
