# GlowLang in 10 minutes

GlowLang reads like plain English, runs on a bytecode VM with a JIT to
native x86-64 and ARM64 code, and comes with its libraries built in. This page
takes you through the whole language in about ten minutes. Every example is in
[`examples/tour.glow`](../examples/tour.glow), so you can run them all at once.

## Install

On Windows, get it from the Microsoft Store:
**https://apps.microsoft.com/detail/9P2D8F466LBX**, or from a terminal:

```powershell
winget install --id 9P2D8F466LBX --source msstore
```

On Linux and macOS, see [getting_started.md](getting_started.md). Then:

```bash
glowlang tour.glow     # run a file
glowlang               # start the REPL
```

## 1. Values

```glow
let name = "Glow"
let year = 2026
let pi = 3.14159
let ready = true
let nothing = none
show f"Hello, {name}! It is {year}."     # Hello, Glow! It is 2026.
```

`let` makes a variable and `show` prints. Text in `f"..."` fills in `{...}`.

## 2. Lists and dicts

```glow
let langs = ["C", "Python", "GlowLang"]
langs.append("Rust")
show langs[2]          # GlowLang
show len(langs)        # 4
show langs[1:3]        # ["Python", "GlowLang"]

let ages = {"ada": 36, "linus": 21}
ages["grace"] = 85
show "ada" in ages     # true
```

## 3. Decisions, two ways

Each block ends with `end`. Else-if is written `eif`.

```text
if score >= 90:            when score >= 90:
    show "A"                   show "A"
eif score >= 70:           eif score >= 70:
    show "B"                   show "B"
else:                      otherwise:
    show "C"                   show "C"
end                        end
```

Both columns compile to the same program. Use whichever reads better to you.

## 4. Loops

```glow
for each lang in langs:
    show "I know " + lang
end

every i from 1 to 3:       # 1, 2 and 3
    show i * i
end

while n < 10:
    n = n + 3
end
```

`skip` moves on to the next pass of a loop and `stop` leaves it.

## 5. Functions

```glow
func area(w, h = 1):
    return w * h
end
show area(3, 4)            # 12
show area(5)               # 5

let double = lambda(x): x * 2
show double(21)            # 42
```

`action` and `can` mean the same as `func`. Hot functions are compiled to
machine code automatically, with nothing for you to switch on: on the
[benchmarks in the README](../README.md#-performance) GlowLang finishes 1.4x to
7.7x sooner than CPython 3.14.

## 6. Classes

```glow
class Counter:
    func init(start):
        self.count = start
    end

    func tick():
        self.count = self.count + 1
        return self
    end
end

let c = Counter(10)
c.tick().tick()
show c.count               # 12
```

## 7. Errors

```glow
try:
    if b == 0:
        throw "cannot divide by zero"
    end
    return a / b
catch e:
    show "caught: " + str(e)
end
```

## 8. More of the language

```glow
# Named arguments, ...rest and ...spread
func label(text, size = 12, bold = false):
    return f"{text} ({size}px, bold: {bold})"
end
show label("Title", bold = true)    # Title (12px, bold: true)

func total(first, ...rest):
    return first + sum(rest)
end
let more = [2, 3, 4]
show total(1, ...more)              # 10

# Decorators: @shout wraps the function below it; **opts collects
# name = value arguments, so the wrapper passes on every argument
func shout(f):
    return lambda(...args, **opts): str(f(...args, **opts)).upper() + "!"
end
@shout
func hello(name, greeting = "hello"):
    return greeting + " " + name
end
show hello("asha", greeting = "hi") # HI ASHA!

# Unpacking
let a, b = 1, 2
a, b = b, a
show a, b                           # 2 1

# match / case and enum
enum Color: RED, GREEN, BLUE
func kind(x):
    match x:
        case 0:
            return "zero"
        case [head, ...tail]:
            return f"list starting {head}"
        otherwise:
            return "other"
    end
end
show kind(0), kind([7, 8]), Color.GREEN    # zero list starting 7 Color.GREEN

# Sets
let seen = {1, 2, 3}
show seen | {3, 4}, 2 in seen       # {1, 2, 3, 4} true

# none-safe access and defaults
let user = none
show user?.name ?? "guest"          # guest

# finally runs however the try is left
try:
    show 7 // 2, -7 // 2            # 3 -4
finally:
    show "cleaned up"
end
```

`//` is floor division, as in Python; comments start with `#`.

## 9. Batteries included

`use "name"` loads a library from `lib/`. These come with the language: HTTP
servers, SQLite, JSON, regex, a C FFI and five machine-learning libraries
written in GlowLang itself.

```glow
use "glownum"
show gnum.dot([[1, 2], [3, 4]], [[1], [1]])    # [[3], [7]]

use "glowtext"
show most_common(tokenize("the cat and the hat and the bat"), 2)
# [["the", 3], ["and", 2]]
```

## Where next

- [language_reference.md](language_reference.md): the full language
- [ai_libraries.md](ai_libraries.md): `glownum`, `glowframe`, `glowlearn`, `glowgrad`, `glowtext`
- [web_guide.md](web_guide.md): web servers and APIs
- [runtime_internals.md](runtime_internals.md): how the VM, JIT and garbage collector work
