# Testing your code: `glowlang test`

Put tests in files named `test_*.glow` (or `*_test.glow`). Each top-level
function whose name starts with `test_` is a test:

```glow
# test_prices.glow
use "prices"

func test_total_adds_tax():
    assert(total(100) == 118, "18% tax on 100")
end

func test_free_items_cost_nothing():
    assert(total(0) == 0, "zero stays zero")
end
```

Run every test file in the current folder and the folders below it:

```bash
glowlang test
```

```
PASS  test_prices.glow :: test_total_adds_tax  (0 ms)
FAIL  test_prices.glow :: test_free_items_cost_nothing
      Assertion failed: zero stays zero
      | debug: total called with 0
2 passed, 1 failed in 0.04 s
```

- A test passes when it returns and fails when it raises: an `assert`
  that is false, a `throw`, or any runtime error. The failure shows the
  error and what the test printed.
- `glowlang test path/to/test_x.glow` runs one file; you can name several
  files and folders. Folders whose name starts with `.` are skipped.
- Each file runs in a process of its own, so a crash in one file does not
  stop the rest. The file's top-level code (its `use` lines, setup) runs
  once, before its tests.
- A test file without `test_` functions is run as a program, and passes when
  it exits with code 0, so tests written as plain scripts work too.
- The exit code is 1 when anything failed, which is what CI needs:

```yaml
- run: glowlang test
```

`assert(condition, message)` is built in. For comparisons that print both
values, write the message with them: `assert(got == want, f"got {got}, want {want}")`.
