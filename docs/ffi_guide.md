# Calling C libraries: `ffi`

`ffi` calls functions in C libraries (`.dll` on Windows, `.so` on Linux,
`.dylib` on macOS) directly, in the same process. Through C it also reaches
libraries written in Rust, Go, Zig and C++ that export C functions
(`extern "C"`). No `import` is needed.

```glow
let libc = ffi.open(ffi.libc())
let c_abs = libc.function("abs", ["int"], "int")
show c_abs(-5)                                   # 5

let libm = ffi.open(ffi.libm())
let cbrt = libm.function("cbrt", ["double"], "double")
show cbrt(27)                                    # 3
```

A declared C function is an ordinary GlowLang function. You can store it in a
variable, dict or object, pass it to other functions, and call it in loops.

**Read this first:** GlowLang checks everything it can, including the number
of arguments, their types and integer ranges. What it cannot check is whether
your declaration matches the C function, or whether a pointer is valid. A
wrong declaration or a bad pointer can crash the program. This is true of
every language's FFI. For Python or JavaScript code, the
[bridge](bridge_guide.md) is the safer choice.

## Libraries

`ffi.open(path)` loads a library and returns it. Calling `ffi.open` again
with the same path is fine.

| Expression | Value |
|---|---|
| `ffi.libc()` | The C runtime: `ucrtbase.dll`, `libc.so.6` or `/usr/lib/libSystem.B.dylib` |
| `ffi.libm()` | The math library: `ucrtbase.dll`, `libm.so.6` or `libSystem` |
| `ffi.lib_name("sqlite3")` | `sqlite3.dll`, `libsqlite3.so` or `libsqlite3.dylib` |

A library has three members:

- `lib.function(name, arg_types, return_type)`: declares a C function. See
  [Types](#types).
- `lib.symbol(name)`: the address of an exported variable or function.
- `lib.path`: the path the library was opened with.

A library stays loaded while any function declared from it is still in use.

**Where libraries are found.** A name with a folder in it (`"./lib/mylib.so"`,
`"C:/tools/x.dll"`) is loaded from that path.

For a bare name (`"sqlite3.dll"`, `"libz.so.1"`):

- On Linux and macOS, the system's normal search applies (`LD_LIBRARY_PATH`,
  the standard folders).
- On Windows, the search covers the application folder, the system folders,
  then `PATH`. The current folder is never searched, so a DLL planted there
  cannot be loaded by accident. Use `"./name.dll"` to load from the current
  folder on purpose.

## Types

| Name | C type | GlowLang value |
|---|---|---|
| `void` | `void` (return only) | `none` |
| `bool` | `bool` | `true` / `false` |
| `int8` `uint8` `int16` `uint16` `int32` `uint32` `int64` `uint64` | fixed-width integers | int |
| `char` `uchar` `short` `ushort` | `char`, `unsigned char`, `short`, `unsigned short` | int |
| `int` `uint` | `int`, `unsigned int` (32-bit) | int |
| `long` `ulong` | `long` (32-bit on Windows, 64-bit elsewhere) | int |
| `longlong` `ulonglong` | `long long` | int |
| `size_t` `ssize_t` `intptr` `uintptr` | pointer-sized integers | int |
| `float` `double` | | float |
| `pointer` | any pointer (`void *`, `int *`, `FILE *` …) | an integer address, or `none` for NULL |
| `string` | `const char *` | text. In: a NUL-terminated copy valid during the call. Out: copied into a GlowLang string, with NULL as `none` |

Integers are checked against their C range. `c_abs(5000000000)` raises an
error instead of passing a truncated value. 64-bit results that don't fit
GlowLang's small integers come back as exact big integers.

`string` parameters accept only text (or `none`). To pass C memory that holds
text, such as a buffer from `ffi.alloc` or `ffi.from_string`, declare the
parameter as `pointer`.

## Pointers and memory

Many C functions take pointers to fill in, called out-parameters, or return
pointers. These helpers work with C memory:

| Function | What it does |
|---|---|
| `ffi.alloc(size)` | Allocate `size` zeroed bytes and return the pointer |
| `ffi.free(ptr)` | Free memory from `ffi.alloc` or `ffi.from_string` (`none` is ignored) |
| `ffi.load(ptr, type, offset)` | Read a value of `type` at `ptr + offset` |
| `ffi.store(ptr, type, value, offset)` | Write a value of `type` at `ptr + offset` |
| `ffi.string(ptr, length)` | The bytes at `ptr` as a string, up to the first NUL when no length is given |
| `ffi.from_string(text)` | A NUL-terminated copy of `text` in C memory |
| `ffi.sizeof(type)` | The size of a type in bytes |
| `ffi.null` | `0`, the NULL pointer |

```glow
# strtol reports where parsing stopped through a char ** out-parameter.
let strtol = libc.function("strtol", ["pointer", "pointer", "int"], "long")
let input = ffi.from_string("42abc")
let endp = ffi.alloc(ffi.sizeof("pointer"))
show strtol(input, endp, 10)                     # 42
show ffi.string(ffi.load(endp, "pointer"))       # abc
ffi.free(endp)
ffi.free(input)
```

**Pointers into a `string` argument don't outlive the call.** A `string`
argument is a temporary copy, freed when the call returns. Here `strtol`'s
end pointer points into its input, so the input is C memory from
`ffi.from_string`, which lives until `ffi.free`. Pass a `string` when C only
reads the text during the call, which is the common case.

Structs are memory too. Read and write their fields at their byte offsets:

```glow
# struct point { int32_t x; int32_t y; double weight; }  (16 bytes)
let pt = ffi.alloc(16)
ffi.store(pt, "int32", 3, 0)
ffi.store(pt, "int32", 4, 4)
ffi.store(pt, "double", 0.5, 8)
some_c_function(pt)
show ffi.load(pt, "double", 8)
ffi.free(pt)
```

Memory from `ffi.alloc` is not freed automatically. Free it once C no longer
uses it.

## Errors from C

`ffi.errno()` is the C `errno` saved right after the last FFI call on the
current thread. On Windows, `ffi.last_error()` is `GetLastError()`, and on
other systems it is the same as `errno`.

```glow
strtol("99999999999999999999999", none, 10)
show ffi.errno() == 34                           # ERANGE
```

## Long-running C calls

By default other GlowLang threads wait while a C function runs. For a call
that blocks for a long time (I/O, sleeping, heavy computation), let them run:

```glow
let slow = lib.function("compress_file", ["string"], "int", {"release_gil": true})
```

Only do this when the C function does not call back into GlowLang.

## Not supported yet

These are refused with an error, never guessed at:

- **Structs passed or returned by value:** pass a pointer instead.
- **Variadic functions** such as `printf`.
- **Callbacks** (C calling a GlowLang function), such as `qsort`'s comparator.
- **More than 16 arguments**, or more than 8 `float`/`double` arguments
  (Linux and macOS).

## Example: SQLite's version through its C API (when SQLite is installed as a shared library)

```glow
let sqlite = ffi.open(ffi.lib_name("sqlite3"))
let version = sqlite.function("sqlite3_libversion", [], "string")
show version()
```
