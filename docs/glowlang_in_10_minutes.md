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
show langs[1:3]        # [Python, GlowLang]

let ages = {"ada": 36, "linus": 21}
ages["grace"] = 85
show "ada" in ages     # true
```

## 3. Decisions, two ways

Each block ends with `end`. Else-if is written `eif`.

```glow
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
machine code automatically, with nothing for you to switch on. Across our 10
benchmarks, GlowLang is about 1.8x faster than CPython 3.14.

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

## 8. Batteries included

`use "name"` loads a library from `lib/`. These come with the language: HTTP
servers, SQLite, JSON, regex, a C FFI and five machine-learning libraries
written in GlowLang itself.

```glow
use "glownum"
show gnum.dot([[1, 2], [3, 4]], [[1], [1]])    # [[3], [7]]

use "glowtext"
show most_common(tokenize("the cat and the hat and the bat"), 2)
# [[the, 3], [and, 2]]
```

## Where next

- [language_reference.md](language_reference.md): the full language
- [ai_libraries.md](ai_libraries.md): `glownum`, `glowframe`, `glowlearn`, `glowgrad`, `glowtext`
- [web_guide.md](web_guide.md): web servers and APIs
- [runtime_internals.md](runtime_internals.md): how the VM, JIT and garbage collector work
