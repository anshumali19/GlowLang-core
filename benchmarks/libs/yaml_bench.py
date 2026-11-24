# The work of yaml_bench.glow with PyYAML: its SafeLoader (written in
# Python) and CSafeLoader (libyaml, written in C).
import time, yaml

text = open(".github/workflows/ci.yml", encoding="utf-8").read()

def measure(label, loader):
    assert yaml.load(text, Loader=loader)["jobs"]["build-and-test"]["runs-on"] == "ubuntu-latest"
    n = 0
    start = time.time()
    while time.time() - start < 2:
        yaml.load(text, Loader=loader)
        n += 1
    print(f"{label:<16}{int(n / (time.time() - start)):,} parses/s")

measure("SafeLoader", yaml.SafeLoader)
measure("CSafeLoader", yaml.CSafeLoader)
