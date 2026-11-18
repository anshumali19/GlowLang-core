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
`#` starts a comment that runs to the end of the line. `//` is not a
comment: it is floor division (see Arithmetic).

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
| String | `"hello"`, `f"hi {x}"` | Unicode text (stored as UTF-8) |
| Boolean | `true`, `false` | Boolean value |
| List | `[1, 2, 3]` | Dynamic array |
| Dictionary | `{"a": 1}` | Hash map |
| None | `none` | Null/absent value |
| Tensor | `tensor.zeros(10)` | SIMD-accelerated numeric array |
| Lambda | `lambda(x): x * 2` | Anonymous function |
| Object | `MyClass()` | Class instance |

**Number literals.** Whole numbers can be written in hex (`0xff`), binary
(`0b1010`) or octal (`0o17`), of any size. An underscore may separate digits
anywhere for readability: `1_000_000`, `3.141_592`, `0xFF_FF`. It must sit
between two digits, so `1_`, `1__0` and `0x_1` are errors, as is a number
run into letters such as `12abc`.

**Whole numbers are integers.** A calculation whose result is a whole number
gives an integer, even from floats: `2.5 * 2` is `5`, `type(2.5 * 2)` is
`"int"`, and `3.0` prints as `3`. `7 / 2` is `3.5` and `6 / 3` is `2`.
Arithmetic, comparisons and `==` treat `1` and `1.0` as the same number, so
this only shows in `type()` and in printed text. To always show decimal
places, format the number: `format(3, ".1f")` is `"3.0"`.

### Type Checking
```glow
let t = type(value)   # "int", "float", "string", "list", "dict", "instance" (an object) ...
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
x //= 2   # x = x // 2
x &= 3    # x = x & 3   (also |=, ^=, <<= and >>=)
x ??= 5   # x = 5 only when x is none
```

They work on fields and items too: `self.count += 1`, `counts[word] += 1`,
`grid[r][c] -= 1`. The object and the index are worked out once, as in Python,
so `next_item().total += 5` calls `next_item()` a single time.

### Unpacking
Several names can be assigned at once. With several values on the right, all
of them are worked out before any name changes, so `a, b = b, a` swaps:
```glow
let a, b = 1, 2
a, b = b, a                          # a is 2, b is 1
let name, age = ["Ada", 36]          # one list: its items, in order
xs[i], xs[j] = xs[j], xs[i]          # list items and fields work too
self.x, self.y = self.y, self.x
```
A single value on the right must be a list with exactly one item per name;
anything else raises an error (`Cannot unpack 3 items into 2 names`).
One name may be `...rest`: it gets the items the others leave, as a list:
```glow
let first, ...others = [1, 2, 3, 4]    # 1 and [2, 3, 4]
let ...init, last = [1, 2, 3]          # [1, 2] and 3
```

### Undefined Names
Reading a name that nothing declares is an error, so a typo is reported where
it happens instead of running on as `none`:
```glow
let total = 10
show totl            # error: name 'totl' is not defined
```
`none` and `pass` (a statement that does nothing) are predefined. A variable
that holds `none` is fine; only names that were never declared are errors. A
global declared further down the file can be used by a function that runs
after that declaration.

---

## 4. Operators

### Arithmetic
| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition (or joining text / lists) | `3 + 4` → `7`, `"a" + 1` → `"a1"` |
| `-` | Subtraction | `10 - 3` → `7` |
| `*` | Multiplication (or repeating text / lists) | `5 * 6` → `30`, `"ab" * 2` → `"abab"` |
| `/` | Division | `15 / 4` → `3.75` |
| `//` | Floor division: the whole number at or below the quotient (exact for integers of any size) | `15 // 4` → `3`, `-7 // 2` → `-4`, `7.5 // 2` → `3` |
| `%` | Modulo (sign of the divisor) | `17 % 5` → `2`, `-7 % 3` → `2` |
| `**` | Power (exact for integers) | `2 ** 10` → `1024`, `2 ** -1` → `0.5` |

Arithmetic on values that are not numbers raises an error (`none * 2` →
"Cannot apply '*' to none and int"). Division and modulo by zero raise errors.

### Bitwise
| Operator | Description | Example |
|----------|-------------|---------|
| `&` | And | `6 & 3` → `2` |
| `\|` | Or | `6 \| 3` → `7` |
| `^` | Exclusive or | `6 ^ 3` → `5` |
| `~` | Not (`~x` is `-x - 1`) | `~6` → `-7` |
| `<<` | Shift left (never overflows: the result grows into a big integer) | `1 << 10` → `1024`, `1 << 100` |
| `>>` | Shift right (rounds down) | `1024 >> 3` → `128`, `-5 >> 1` → `-3` |

They work on whole numbers of any size, the way Python does (a negative
number acts as two's complement with endless sign bits). Floats, booleans
and text raise an error, and so does a negative shift count. They bind more
tightly than comparisons and less tightly than `+` and `-`, from loosest to
tightest `|`, `^`, `&`, then `<<` / `>>`: `flags & WRITE != 0` means
`(flags & WRITE) != 0`, and `1 + 2 << 1` is `6`.

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

### None: `??` and `?.`
`a ?? b` is `a` unless `a` is `none`, then `b`. Unlike `or`, it keeps
`0`, `false` and `""`. `b` runs only when it is needed:
```glow
let port = cfg.port ?? 80          # 0 stays 0; a missing port gives 80
let name = user.nick ?? user.name ?? "guest"
```
`a?.b`, `a?.method(args)` and `a?.[i]` give `none` when `a` is `none`,
and skip the rest of the chain, arguments included, so
`user?.address.city` is `none` for a `none` user instead of an error.
Parentheses end the chain: `(user?.address).city` fails when the
inner part is `none`.
```glow
let timeout = options?.timeout ?? 30
show order?.items?.[0]?.name ?? "no items"
```
`x ??= value` sets `x` only when it is `none`. `??` binds more loosely than
`or` and more tightly than `when ... otherwise`.

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

After the expression, `:` and a format spec control how it is written, the
same mini-language as `format()` below:
```glow
let price = 1234.5
show f"Total: {price:,.2f}"     # Total: 1,234.50
show f"[{7:>3}] [{0.256:.1%}]"  # [  7] [25.6%]
```
A string literal inside the braces is not supported yet; put it in a
variable first. An expression that cannot be read raises an error saying so.

### String Operations
```glow
let s = "Hello, World"
let length = len(s)          # String length
let ch = s[0]                # Character by index (s[-1] is the last); out of range raises
let sub = s[0:5]             # Slicing: "Hello"
let parts = s.split(", ")    # Split into a list
```

### Unicode
Strings hold Unicode text (stored as UTF-8), and everything that counts or
indexes works in characters (code points), as in Python: `len`, `s[i]`,
slices, `find`, `split("")`, `for each ch in s`, `pairs(s)` and regex match
positions.
```glow
let s = "héllo 😀"
show len(s), s[1], s[-1], s[0:5]    # 7 é 😀 héllo
show s.find("😀"), s.byte_len()      # 6 11
show "straße".upper(), "ΑΒΓ".lower() # STRASSE αβγ
```
`s.byte_len()` and `s.bytes()` (a list of numbers 0-255) give the UTF-8
bytes, for protocols and binary data: an HTTP `Content-Length`, say, or
`crypto.random_bytes(n)`, which returns n raw bytes. `upper()` and `lower()`
use the full Unicode case tables (including mappings to several characters,
as `ß` to `SS`), without locale or context rules such as Greek final sigma.
A byte that is not valid UTF-8 counts as one character, so no input is lost.
Indexing pure-ASCII text is constant-time, and walking any string with
`s[i]` from start to end takes linear time.

### String Methods
| Method | Result |
|--------|--------|
| `s.upper()`, `s.lower()` | Changed case |
| `s.trim()` (`strip`), `s.trim_start()`, `s.trim_end()` | Whitespace removed; `s.trim("-=")` removes those characters instead |
| `s.replace(old, new)` | Every occurrence replaced |
| `s.contains(x)`, `s.starts_with(x)`, `s.ends_with(x)` | `true` / `false` |
| `s.find(x)` | Position of `x` (in characters), or `-1` |
| `s.count(x)` | Number of occurrences |
| `s.split(sep)`, `s.lines()` | List of parts; `s.split()` with no separator gives the words (split at any spaces, tabs and new lines, none empty) |
| `sep.join(list)` | Items joined with `sep` |
| `s.is_number()` | Whether `float(s)` would succeed |
| `s.byte_len()`, `s.bytes()` | Size in UTF-8 bytes; the bytes as a list of numbers |

---

## 6. Lists

Inside a list or dict, strings show in quotes (with `
`, `	`, `\"`
escaped), so different values always look different: `show ["a, b", "c"]`
prints `["a, b", "c"]`, not `[a, b, c]`. A string on its own still shows
as plain text: `show "hi"` prints `hi`.

### Creation
```glow
let empty = []
let nums = [1, 2, 3, 4, 5]
let mixed = [1, "two", 3.0, true]
let joined = [0, ...nums, 6]     # ...spreads another list's items in
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

A third part is the step: `[start:stop:step]` takes every `step`-th item. A
negative step walks backwards, and start and stop then default to the far
ends. Slices work the same on strings, by characters, and follow Python's
rules exactly:
```glow
let nums = [0, 1, 2, 3, 4, 5]
show nums[::2]               # [0, 2, 4]
show nums[1::2]              # [1, 3, 5]
show nums[::-1]              # [5, 4, 3, 2, 1, 0]
show "glow"[::-1]            # wolg
```
A step of 0 is an error.

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

Keys are text. A number or `true`/`false` used as a key is stored as its
text, so `d[1]` and `d["1"]` are the same entry and `d.keys()` gives
`["1"]`. Other values (`none`, lists, dicts, objects) cannot be keys and
raise an error.

Dictionaries keep insertion order: `keys()`, `values()`, `for each` loops,
`show` and `json.stringify` list entries in the order they were added.
Updating a key keeps its place; removing a key and adding it again moves it
to the end. `json.parse` keeps the document's order.

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

### Dot Access
`user.name` reads the key `"name"`. A missing key reads as `none` (handy for
option dictionaries such as `opts.timeout`); use `user.has("name")` to tell
a missing key from one that holds `none`. Other values are strict: reading a
property a string, list, number, object or `none` does not have is an error:
```glow
"glow".length        # error: string has no property 'length' (use len(x) for its length)
none.name            # error: cannot read property 'name' of none
```

### Sets
A set holds each item once, in the order the items were first added.
`{a, b}` makes one (`{}` is still an empty dict), and so does `set(items)`
from a list, the characters of a string, a dict's keys or another set:
```glow
let tags = {"red", "green"}
let seen = set([3, 1, 3, 2])     # {3, 1, 2}
tags.add("blue")
show "red" in tags, len(seen)    # true 3
for each t in tags:
    show t
end
```
| Operation | Result |
|-----------|--------|
| `a \| b`, `a.union(b)` | Items in either |
| `a & b`, `a.intersection(b)` | Items in both |
| `a - b`, `a.difference(b)` | Items in `a` but not `b` |
| `a ^ b`, `a.symmetric_difference(b)` | Items in exactly one |
| `add(x, ...)`, `remove(x)` (error if missing), `discard(x)`, `clear()` | Change the set |
| `has(x)` / `contains(x)`, `is_subset(b)`, `is_superset(b)`, `is_disjoint(b)` | `true` / `false` |
| `copy()`, `to_list()`, `is_empty()`, `len(s)` | |

The methods also take a list where they need another set. Equal numbers
are one item (`{1, 1.0}` is `{1}`), but `1`, `"1"` and `true` are three.
Lists, dicts and sets cannot be items, because they can change; objects
can, and each object is its own item. `==` compares the items, and
`sorted`, `min`, `max`, `sum` and `...spread` accept sets.

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

### Match
`match` runs the first `case` whose pattern fits the value, or the
`otherwise` block when none does:
```glow
match command:
    case "start", "go":              # equal to any of these
        start()
    case ["move", x, y]:             # a list of three: captures x and y
        move(x, y)
    case ["say", ...words]:          # ...words gets the rest of the list
        show " ".join(words)
    case n when type(n) == "int" and n > 100:   # a bare name captures; when adds a test
        show "big number", n
    case _:                          # anything
        show "unknown"
end
```
In a pattern, a bare name captures the value and `_` matches anything.
`none`, numbers, text, dotted names such as `Color.RED`, and other expressions
are compared with `==`. To compare with a variable, use a guard:
`case v when v == limit:`. `stop`, `skip` and `return` inside a case work as
anywhere else. `match`, `case` and `enum` remain ordinary names outside
these statements.

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

With names in brackets, each item (a list) is unpacked into them:
```glow
for each [name, score] in zip(names, scores):
    show name, score
end
```
`for each i, item in list` (no brackets) still gives the index and the item,
and `for each key, value in dict` the key and the value.

### Every Loop (Range-based)
`every i from a to b` counts up from `a` to `b`, both included. When `b` is
less than `a` it runs zero times (so `every i from 1 to len(items)` is safe
for an empty list); to count down, loop over `range(10, 0, -1)`.
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
Functions without an explicit `return` statement return `none`, and so does
a function that ends without reaching one.

### Variables in Functions
A function reads and changes a global that was declared (with `let`) before
the function. Assigning to any other name inside a function makes a local
variable, the way Python does, so declare globals a function changes above
the function:
```glow
let count = 0
func bump():
    count = count + 1     # the global, declared above
end
```

### Default Parameter Values
A parameter can have a default value, used when the caller leaves that
argument out (or passes `none`). A parameter with no default that the caller
leaves out is `none`. Passing more arguments than a function, method,
lambda or class `init` declares is an error, such as
`greet() takes 2 arguments but 3 were given`.

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

### Named Arguments
An argument can be passed by its parameter's name, after the positional
ones. Parameters left out in between get their defaults:
```glow
greet("Ana", mark = ".")           # Hello, Ana.
greet(mark = "?", name = "Bo")     # Hello, Bo?
let p = Point(y = 5)               # constructors (init's parameters) too
```
A name that is not a parameter, or one that a positional argument already
filled, is an error (`greet() has no parameter named 'nope'`,
`greet() got two values for 'name'`). Built-in functions take positional
arguments only.

### Any Number of Arguments: `...rest`
A last parameter written `...name` collects the extra arguments into a list
(empty when there are none). It works for functions, lambdas, methods and
`init`:
```glow
func total(first, ...rest):
    return first + sum(rest)
end
total(1)            # 1, rest is []
total(1, 2, 3)      # 6, rest is [2, 3]
let count = lambda(...xs): len(xs)
```

### Spreading a List: `...list`
`...list` in a call passes the list's items as separate arguments; in a list
literal it puts them in place:
```glow
let nums = [5, 6, 7]
total(...nums)          # total(5, 6, 7)
max(...nums, 10)
let more = [0, ...nums, 8]   # [0, 5, 6, 7, 8]
let copy = [...nums]         # a new list
super.init(...parts)         # inside a method
```
Spreading anything other than a list or a set is an error.

### Generators: `yield`
A function whose body contains `yield` is a generator function. Calling it runs
none of its code yet: it returns a generator, and the body runs a piece at a
time as the generator is stepped, pausing at each `yield` and handing over its
value. Values are made only when they are needed, so a generator can be
endless.

```glow
func count_up(limit):
    let i = 0
    while i < limit:
        yield i
        i += 1
    end
end

for each n in count_up(3):
    show n                       # 0, then 1, then 2
end
show [n * n for each n in count_up(4)]   # [0, 1, 4, 9]

let g = count_up(2)
show next(g), next(g)            # 0 1
show next(g, "done")             # done: g has no more values

func naturals():
    let n = 1
    while true:
        yield n
        n += 1
    end
end
for each n in naturals():
    when n > 3:
        stop                     # leaves the generator paused
    end
    show n
end

func chain(a, b):
    yield from a                 # each item of a list, set, string or generator
    yield from b
end
show [v for each v in chain([1, 2], count_up(2))]   # [1, 2, 0, 1]
```

- `next(g)` gives the next value and raises once `g` has finished;
  `next(g, default)` returns `default` instead.
- `return` ends a generator. A bare `yield` hands over `none`.
- Methods and lambdas can be generators. Inside a method, `self` works as
  usual.
- An error raised in the body reaches whoever is stepping the generator, and
  the generator is then finished.
- `for each a, b in g` needs positions, which a generator does not have; write
  `for each [a, b] in g` to take each value apart.
- A generator that is never stepped to its end does not run the rest of its
  body, `finally` blocks included.
- `yield` is a keyword only at the start of a statement, so a variable called
  `yield` still works. `super` cannot be used inside a generator method.

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

When the body needs more than one expression, end the header line with `:`,
write the statements, and close with `end`. This works anywhere a value goes,
including inside a call's parentheses, a list or a dict:

```glow
show [3, 1, 2].map(lambda(x):
    let sq = x * x
    return sq + 1
end)              # [10, 2, 5]

let handlers = {
    "double": lambda(x):
        return x * 2
    end
}
show handlers["double"](21)    # 42
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

### Showing an object: `to_string`
An object whose class has a `to_string()` method shows as the text it
returns: in `show`, `str()`, f-strings and inside lists. It must return
text.
```glow
class Point:
    func init(x, y):
        self.x = x
        self.y = y
    end
    func to_string():
        return "(" + str(self.x) + ", " + str(self.y) + ")"
    end
end
show Point(1, 2)        # (1, 2)
```

### Inheritance
`extends` makes a class inherit another's methods, `init` included. A
method defined in the child replaces the parent's, and chains of any depth
work:
```glow
blueprint Animal:
    action init(name):
        self.name = name
    end
    action speak():
        return self.name + " makes a sound"
    end
end

blueprint Dog extends Animal:
    action speak():
        return self.name + " barks"
    end
end

show Dog("Rex").speak()    # Rex barks (init comes from Animal)
```
Extending something that is not a class raises an error.

### Calling the parent: `super`
Inside a method, `super.name(args)` runs the parent class's version of a
method on the same object. It is how a child's `init` lets the parent set up
its part, and how an override adds to the parent's behaviour instead of
replacing it:
```glow
blueprint Puppy extends Dog:
    action init(name, age):
        super.init(name)           # Animal's init sets self.name
        self.age = age
    end
    action speak():
        return super.speak() + " softly"
    end
end

show Puppy("Bo", 1).speak()    # Bo barks softly
```
`super` looks the method up from the parent upward, so it also finds a
method the parent itself inherited. It works only inside a method of a class
that extends another; anywhere else it raises an error saying so.

### Checking classes: `is_a` and `class_name`
`type(x)` is `"instance"` for every object. To ask about its class:
```glow
let p = Puppy("Bo", 1)
show class_name(p)          # Puppy
show is_a(p, Dog)           # true: Puppy extends Dog
show is_a(p, Animal)        # true: and Dog extends Animal
show is_a(p, "Animal")      # true: a class name works too
```

---

### Enums
```glow
enum Color:
    RED
    GREEN
    BLUE = 10        # members without a value count on from the one before
end
enum Dir: UP, DOWN   # on one line

show Color.RED, Color.GREEN.value, Color.BLUE.name    # Color.RED 1 BLUE
show Color.RED == Color.RED, Color["GREEN"]           # true Color.GREEN
for each name, member in Color:
    show name, member.value
end
```
An enum is a dict from each name to its member. Each member has a `name`
and a `value` and shows as `Color.RED`. Members are equal only to
themselves, work in `match` (`case Color.RED:`) and in sets, and
`class_name(Color.RED)` is `"Color"`. The first member's default value is
0.

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

### Finally
A `finally` block runs however the `try` is left: after the body finishes,
after `catch` handles an error, when an error is not caught (it then goes
on), and when `return`, `stop` or `skip` leaves early. Use it for cleanup
that must always happen, such as closing a file or a connection:
```glow
let db = sqlite.open("shop.db")
try:
    sqlite.exec(db, "insert into orders (item) values ('pen')")
catch e:
    show "could not save: " + str(e)
finally:
    sqlite.close(db)          # runs in every case
end
```
`catch` is optional (`try:` ... `finally:` ... `end`). A `return` value is
worked out before `finally` runs, and a `return` inside `finally` replaces
it. An error raised inside `finally` is not caught by its own `catch`; it
goes to the enclosing `try`. An error that passes through a `finally` is
reported at the line where it was raised.

### With
`with` uses a resource for the length of a block and lets go of it however
the block is left: when it ends, when it raises, and on `return`, `stop` or
`skip`:
```glow
with sqlite.open("shop.db") as db:
    sqlite.exec(db, "create table if not exists orders (item text)")
    sqlite.exec(db, "insert into orders (item) values ('pen')")
end                           # db is closed here, error or not

use "lib/sync.glow"
let m = Mutex()
with m:
    show m.is_locked()        # true: held for the block
end
show m.is_locked()            # false
```
An object takes part through its methods:
- `enter()` (optional) gives the value bound by `as`; without it, `as`
  binds the object itself.
- `exit(error)` runs when the block is left, with the error or `none`. If it
  returns `true` for an error, the error is swallowed; otherwise it goes on.
  `exit()` may leave out the parameter.
- An object with no `exit()` but a `close()` method is closed instead (a
  Postgres connection, a WebSocket, a `Channel`). SQLite handles are closed
  too.

`with a() as x, b() as y:` opens both, in order, and lets go of them in
the opposite order. If `enter()` raises, the block and `exit()` do not run.
`Mutex` (lock and unlock) and the ORM's `Transaction` (commit, or roll back
when the block raises) work with `with`. Using `with` on a value with no
`exit()` or `close()` is an error.

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

### Packages (gdu)
`import` and `use` read local files only; they never download anything. To
use a published package, install it with `gdu` (the GlowLang package
manager), which puts it in `.glow_pkg/` next to your program:

```bash
gdu install middleware
```

```glow
use "middleware"     # found in .glow_pkg/ before the standard library
```

The packages available are the folders of
[GlowLang-packages/packages](https://github.com/anshumali19/GlowLang-packages/tree/main/packages).

A name without `.glow` is looked up next to the importing file, then in
`.glow_pkg/`, then in the standard library.

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

### Dict and Set Comprehensions
The same form in braces makes a dict (with `key: value`) or a set:
```glow
let lengths = {w: len(w) for each w in ["a", "bb", "ccc"]}   # {"a": 1, "bb": 2, "ccc": 3}
let big = {k: lengths[k] for each k in lengths.keys() when lengths[k] > 1}
let squares = {x * x for each x in [-2, -1, 0, 1, 2]}          # {4, 1, 0}
```

---

## 18. Standard Library

Every name below exists in the runtime (checked against `<module>.keys()`).

### Built-in Functions
| Function | Description |
|----------|-------------|
| `show a, b` / `print` | Print values separated by spaces |
| `len(x)` | Length of a string, list or dict |
| `type(x)` | `"int"`, `"float"`, `"string"`, `"bool"`, `"list"`, `"dict"`, `"none"`, `"instance"` (an object), `"class"`, ... |
| `class_name(x)` | The class of an object (`"Dog"`), a class's own name, or `type(x)` for other values |
| `is_a(x, C)` | Whether `x` was made from class `C` or one that extends it (`C` may be a class or its name) |
| `int(x)`, `float(x)`, `str(x)` / `string(x)`, `bool(x)` | Conversions; `int("42")`, `int("ff", 16)` parse text and raise on anything that is not a whole number |
| `range(n)`, `range(start, stop)`, `range(start, stop, step)` | Whole numbers from start (default 0) up to, not including, stop: `range(0, 10, 3)` is `[0, 3, 6, 9]`, `range(5, 0, -1)` is `[5, 4, 3, 2, 1]` |
| `enumerate(items, start)` | `[[0, first], [1, second], ...]` (counting from `start`, default 0) |
| `zip(a, b, ...)` | `[[a0, b0], [a1, b1], ...]`, as long as the shortest list |
| `format(value, spec)` | Text for a value: `format(3.14159, ".2f")` is `"3.14"`. Spec: `[[fill]align][sign][0][width][,][.precision][type]` with align `<` `>` `^` `=`, sign `+` or space, type `d` `f` `e` `g` `%` `x` `X` `o` `b` `s` |
| `append(list, x)` | Add to a list |
| `pairs(x)` | `[index, item]` / `[key, value]` pairs |
| `abs`, `min`, `max`, `sum`, `round`, `floor`, `ceil` | Numbers (`min`/`max` take values or one list; `round(x, 2)` rounds to 2 decimal places, `round(x, -2)` to hundreds) |
| `random()`, `random_int(lo, hi)`, `random_seed(n)` | A float in [0, 1); a whole number from lo to hi inclusive; restart the sequence for repeatable runs (xoshiro256\*\*; for secrets use `crypto.random_bytes`) |
| `sorted(list, key, descending)`, `reversed(x)` | New ordered list (or reversed string) |
| `input(prompt)` / `ask(prompt)` | Read a line from standard input (`none` at end of input) |
| `assert(condition, message)` | Raise an error ("Assertion failed: message") when the condition is false; `try`/`catch` can handle it |
| `dict()` | Empty dictionary |
| `set()`, `set(items)` | A set (see Sets) |

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
Server: `listen(port)`, `accept(server)`, `read(client)`, `send(client, data)`, `close(handle)`.

Client: `request(method, url, options)` returns `{"status", "body", "headers", "url"}`
(`url` is where redirects ended). Options: `headers` (dict), `body`, `timeout`
(seconds, default 30), `follow_redirects` (default true), `max_redirects`
(default 10), `verify` (default true). HTTPS checks the server's certificate
and host name against the system's trusted roots; `"verify": false` turns
that off for test servers. Chunked responses are decoded. A failure raises an
error that says what went wrong (cannot resolve host, timed out, certificate
not trusted, ...). `lib/http_client.glow` builds on this.

```glow
let r = http.request("GET", "https://example.com/", {"timeout": 10})
show r["status"], len(r["body"])
```

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
no catastrophic backtracking. `.`, sets (`[é-ë]`, `[^a]`) and literals match
whole characters, and positions count characters like string indexes; `\d \w
\s`, `\b` and `(?i)` cover ASCII only (as Python's `re.ASCII`).

### OS (`os.`)
`time()`, `format_time(t)`, `sleep(ms)` (fractions allowed; releases the GIL), `system(cmd)`,
`exit(code)`, `gc_info()`.
Scripts: `args` (the arguments after the program file), `script` (its path),
`getenv(name, default)` / `env(name)`, `env()` (all variables as a dict),
`setenv(name, value)`, `unsetenv(name)`, `cwd()`, `chdir(dir)`, `home()`,
`temp_dir()`, `pid()`, `platform` (`"windows"`, `"linux"`, `"macos"`),
`isatty(fd)`, `read_stdin()`, `write(values...)` (no newline),
`eprint(values...)` / `ewrite(values...)` (standard error). See the
[shell scripting guide](shell_scripting.md).

### Files (`fs.`)
`read(path)`, `write(path, text)`, `append(path, text)`, `exists(path)`,
`is_file(path)`, `is_dir(path)`, `remove(path)`, `remove_all(path)`
(a directory and everything in it), `mkdir(path)`, `mkdirs(path)` (with
parents), `copy(src, dst)`, `move(src, dst)`, `list(dir)` / `readdir(dir)`,
`walk(dir)` (every file below dir), `glob(pattern)` (`*`, `?`, `[a-z]`,
`**`), `size(path)`, `stat(path)`, and path pieces `join(a, b, ...)`,
`basename`, `dirname`, `extension`, `abspath`.

`chr(code)` and `ord(text)` convert between characters and Unicode code
points.

### Crypto (`crypto.`)
`sha256(data)`, `hmac(key, data)` / `hmac_sha256` (HMAC-SHA256), `blake3(data)`: hex digests
of the whole string, including any zero bytes. `hmac(key, data, hash, encoding)` takes `hash`
`"sha1"`, `"sha256"` (the default), `"sha384"` or `"sha512"` and `encoding` `"hex"` (the
default), `"base64"` or `"raw"` (the bytes themselves); Twilio's webhooks, for example, are
`crypto.hmac(token, data, "sha1", "base64")`. `random_bytes(n)` and `random_hex(n)`: n bytes
from the operating system's secure random source, for keys and tokens. `equal(a, b)`: compare
secrets in constant time. `uuid4()`: a random UUID. For signing messages, see `lib/kavach.glow`.

### Threads (`threads.`)
`join_all()`, `active()`. See also `lib/async.glow` and `lib/sync.glow`.

### Debugging (`debug.`)
`assert(condition, message)`, `stacktrace()`, `dump_stack()`, `profile_start()`, `profile_stop()`.

### Dates and Times (`import "datetime"`)
```glow
import "datetime"
let t = datetime.parse("2026-10-31T14:05:00+05:30")
show t.year, t.month, t.day, t.hour, t.weekday_name()   # 2026 10 31 14 Saturday
show t.to_utc().iso()                                   # 2026-10-31T08:35:00Z
show t.add_days(1).format("%d %B %Y")                   # 01 November 2026
let d = datetime.parse("31/10/2026 9:30 PM", "%d/%m/%Y %I:%M %p")
let deadline = datetime.create(2026, 12, 31, 23, 59)    # local time
show deadline.diff(datetime.now()) / 86400, "days left"
```
`datetime.parse(text)` reads ISO 8601 (`2026-10-31`, `2026-10-31T14:05`,
`2026-10-31 14:05:09.250Z`, `...+05:30`); `datetime.parse(text, format)`
takes strptime codes. Also `datetime.now()`, `datetime.utcnow()`,
`datetime.create(year, month, day, hour, minute, second, microsecond, offset)`,
`datetime.from_timestamp(seconds, offset)`, `datetime.days_in_month(y, m)` and
`datetime.is_leap(y)`.

A DateTime has `.timestamp` (Unix seconds), `.offset` (seconds east of UTC;
local time when none is given, daylight saving included), `.year` ...
`.microsecond`, `.weekday` (Monday 0) and `.yearday`, and the methods
`format`, `iso`, `to_string`, `weekday_name`, `month_name`, `add_seconds`,
`add_minutes`, `add_hours`, `add_days`, `add_months`, `add_years`, `diff`,
`before`, `after`, `equals`, `start_of_day`, `to_utc`, `to_local` and
`with_offset`. Adding months clamps to the month's last day (Jan 31 + 1
month is Feb 28 or 29). Format codes: `%Y %y %m %d %e %H %I %p %M %S %f %B
%b %A %a %j %z %Z %F %T %u %w %s %%`. Named time zones (`Asia/Kolkata`) are
not supported; use offsets. Parsing and formatting are built in, so they
behave the same on every platform.

### WebSockets (`use "websocket"`)
`websocket.connect(url, options)` opens a `ws://` or `wss://` connection;
`websocket.serve(port, handler)` runs `handler(ws)` for each connection
(GlowWeb: `app.websocket(path, handler)`). A connection has `send`,
`send_bytes`, `send_json`, `receive(seconds)` (a dict with `type` "text",
"binary" or "close"), `receive_json`, `ping`, `close(code, reason)` and
`is_open`. See [WebSockets](websocket.md).

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
