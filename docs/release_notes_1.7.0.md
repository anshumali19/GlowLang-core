# GlowLang 1.7.0

## New

- Five AI libraries written in GlowLang itself: `glownum` (like NumPy),
  `glowframe` (like pandas), `glowlearn` (like scikit-learn), `glowgrad`
  (autograd and neural networks, like PyTorch) and `glowtext` (text for
  machine learning). See [ai_libraries.md](ai_libraries.md).

## Faster

- The compiler now handles `not`, `in`, slices, `skip` and `stop`, and
  `str(int)` and function calls are quicker.
- `d["key"]`, `len(list)`, f-strings and two-variable loops are faster.
- On x86-64, a JIT-compiled function that calls itself, or calls another
  compiled function, stays in native code.

## Fixed

- `return f()` inside a `try` body is now caught by that `try`.
- `http.read` returns the whole request; `has()` and `in` find keys whose
  value is `none`; a hint is shown when `elif` is written instead of `eif`.
- The optimizer no longer reuses a stale constant, which made
  `(1 + 0.5 + 1) / 3` give 1.33.
- A method called with fewer arguments than it declares no longer sees
  leftover values in the missing ones.
- A tail call into a function with more local variables no longer
  corrupts the stack ("Attempted to call a non-method value").
- The JIT no longer overflows on functions that use more than 256
  registers ("stack smashing detected").
