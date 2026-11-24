# The work of semver_bench.glow with Python's packaging library (written in
# Python). It follows PEP 440 rather than semver, but the work is the same:
# parse versions, compare them, test a range, sort.
import random, time
from packaging.version import Version
from packaging.specifiers import SpecifierSet

def measure(label, f):
    n = 0
    start = time.time()
    while time.time() - start < 2:
        f()
        n += 1
    print(f"{label:<14}{int(n / (time.time() - start)):,} calls/s")

random.seed(3)
versions = [f"{random.randint(0, 9)}.{random.randint(0, 30)}.{random.randint(0, 99)}" for _ in range(1000)]

measure("Version", lambda: Version("1.4.0b2+exp"))
measure("compare", lambda: Version("1.10.2") < Version("1.9.12"))
# A range is parsed on each call, as semver.satisfies does.
measure("contains", lambda: SpecifierSet(">=1.2.3,<2.0.0").contains("1.5.0"))
measure("sort 1,000", lambda: sorted(versions, key=Version))
