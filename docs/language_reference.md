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
Statements are terminated by newlines. No semicolons required.

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
| Integer | `42`, `-7`, `0` | 64-bit signed integer |
| Float | `3.14`, `-0.5`, `1.0` | 64-bit double-precision |
| String | `"hello"`, `f"hi {x}"` | Rope-backed UTF-8 string |
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
| `+` | Addition | `3 + 4` → `7` |
| `-` | Subtraction | `10 - 3` → `7` |
| `*` | Multiplication | `5 * 6` → `30` |
| `/` | Division | `15 / 4` → `3.75` |
| `%` | Modulo | `17 % 5` → `2` |

### Comparison
| Operator | Description | Example |
|----------|-------------|---------|
| `==` | Equal | `x == 5` |
| `!=` | Not equal | `x != 5` |
| `<` | Less than | `x < 10` |
| `>` | Greater than | `x > 0` |
| `<=` | Less or equal | `x <= 10` |
| `>=` | Greater or equal | `x >= 0` |

### Logical
| Operator | Description | Example |
|----------|-------------|---------|
| `and` | Logical AND | `a and b` |
| `or` | Logical OR | `a or b` |
| `not` | Logical NOT | `not a` |

### Identity
| Operator | Description | Example |
|----------|-------------|---------|
| `is` | Identity check | `x is true` |
| `is not` | Negative identity | `x is not none` |

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
```

### F-Strings (Interpolation)
```glow
let name = "GlowLang"
let version = 1
show f"Welcome to {name} v{version}!"
```

### String Operations
```glow
let s = "Hello, World"
let length = len(s)         # String length
let ch = s[0]               # Character access by index
let sub = s[0:5]            # Slicing: "Hello"
let parts = s.split(", ")   # Split into list
```

### Rope-Backed Strings
GlowLang strings use a Rope data structure internally, providing O(log n) concatenation and efficient manipulation of large strings.

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

### Slicing
```glow
let sub = nums[1:3]          # Elements at index 1, 2
let from2 = nums[2:]         # From index 2 to end
let to3 = nums[:3]           # From start to index 2
```

### List Operations
```glow
append(nums, 6)              # Append element
let length = len(nums)       # Length
let r = range(10)            # Create [0, 1, 2, ..., 9]
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
```

### Dictionary Methods
```glow
let has = user.has("name")        # Check key existence
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
Lambdas capture variables from their enclosing scope:

```glow
func make_counter():
    let count = 0
    return lambda():
        count = count + 1
        return count
    end
end

let counter = make_counter()
show counter()   # 1
show counter()   # 2
```

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

### Task Declaration
```glow
task fetch_data(url):
    let response = http.get(url)
    show "Fetched:", response
end

spawn fetch_data("http://api.example.com/data")
```

### Concurrency Model
GlowLang uses OS-level threads with a Global Interpreter Lock (GIL). The GIL is automatically released during I/O operations (`os.sleep`, `http.*`, `sqlite.*`) allowing true concurrency for I/O-bound workloads.

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

### Output
| Function | Description |
|----------|-------------|
| `show value` | Print value(s) to stdout |
| `show a, b, c` | Print multiple values separated by spaces |

### Math
| Function | Description |
|----------|-------------|
| `sqrt(n)` | Square root |
| `abs(n)` | Absolute value |
| `math.vec2_dist(x1,y1,x2,y2)` | 2D Euclidean distance |

### Collections
| Function | Description |
|----------|-------------|
| `len(x)` | Length of string, list, or dict |
| `range(n)` | Create list `[0, 1, ..., n-1]` |
| `append(list, item)` | Append item to list |
| `type(x)` | Return type as string |

### Tensor / AI
| Function | Description |
|----------|-------------|
| `tensor.zeros(n)` | Create zero-filled tensor of size n |
| `tensor.create(n)` | Create uninitialized tensor |
| `tensor.add(a, b)` | Element-wise addition |
| `tensor.mul(a, b)` | Element-wise multiplication |
| `tensor.dot(a, b)` | Dot product |
| `tensor.matmul(A,r1,c1,B,r2,c2)` | Matrix multiplication |
| `tensor.transpose(A,rows,cols)` | Matrix transposition |

### HTTP / Networking
| Function | Description |
|----------|-------------|
| `http.listen(port)` | Start TCP listener on port |
| `http.accept(server)` | Accept incoming connection |
| `http.read(client)` | Read request data |
| `http.send(client, data)` | Send response data |
| `http.close(handle)` | Close connection/server |

### Database (SQLite)
| Function | Description |
|----------|-------------|
| `sqlite.open(path)` | Open SQLite database |
| `sqlite.exec(db, query)` | Execute SQL statement |
| `sqlite.query(db, query)` | Execute query, return rows |
| `sqlite.close(db)` | Close database connection |

### Database (Redis)
| Function | Description |
|----------|-------------|
| `redis.connect(host, port)` | Connect to Redis server |
| `redis.set(conn, key, value)` | Set key-value pair |
| `redis.get(conn, key)` | Get value by key |

### JSON
| Function | Description |
|----------|-------------|
| `json.stringify(obj)` | Convert object to JSON string |
| `json.parse(str)` | Parse JSON string to object |

### OS
| Function | Description |
|----------|-------------|
| `os.time()` | Current time in seconds (float) |
| `os.sleep(ms)` | Sleep for milliseconds (non-blocking, yields GIL) |

### File System
| Function | Description |
|----------|-------------|
| `fs.read(path)` | Read file contents as string |
| `fs.write(path, content)` | Write string to file |
| `fs.exists(path)` | Check if file exists |

### GUI (X11)
| Function | Description |
|----------|-------------|
| `gui.open()` | Open X11 window |
| `gui.clear()` | Clear the window |
| `gui.draw_rect(x,y,w,h)` | Draw rectangle |
| `gui.poll()` | Poll for events |

### Collision Detection
| Function | Description |
|----------|-------------|
| `collision.aabb(x1,y1,w1,h1,x2,y2,w2,h2)` | Axis-Aligned Bounding Box check |

### Debug
| Function | Description |
|----------|-------------|
| `debug.stacktrace()` | Print the current call stack |

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
