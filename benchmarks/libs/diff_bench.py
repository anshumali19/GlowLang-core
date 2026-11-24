# The work of diff_bench.glow with Python's difflib (written in Python).
import difflib, time

a = [f"line {i} of the file" for i in range(5000)]
b = list(a)
for e in range(1, 51):
    at = e * 97
    if e % 3 == 0:
        b[at] = f"changed {e}"
    elif e % 3 == 1:
        b.insert(at, f"inserted {e}")
    else:
        del b[at]
ta = "\n".join(a) + "\n"
tb = "\n".join(b) + "\n"
n = 0
start = time.time()
while time.time() - start < 2:
    "".join(difflib.unified_diff(ta.splitlines(True), tb.splitlines(True), "a", "b"))
    n += 1
print(f"{'difflib.unified_diff':<22}{n / (time.time() - start):.1f} diffs/s")
