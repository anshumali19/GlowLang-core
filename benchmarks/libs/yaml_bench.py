# The work of yaml_bench.glow with PyYAML: its SafeLoader and SafeDumper
# (written in Python) and CSafeLoader and CSafeDumper (libyaml, written in C).
import time, yaml

text = open(".github/workflows/ci.yml", encoding="utf-8").read()
config = yaml.load(text, Loader=yaml.SafeLoader)
assert config["jobs"]["build-and-test"]["runs-on"] == "ubuntu-latest"

def measure(label, work):
    n = 0
    start = time.time()
    while time.time() - start < 2:
        work()
        n += 1
    print(f"{label:<16}{int(n / (time.time() - start)):,} per second")

measure("SafeLoader", lambda: yaml.load(text, Loader=yaml.SafeLoader))
measure("CSafeLoader", lambda: yaml.load(text, Loader=yaml.CSafeLoader))
measure("SafeDumper", lambda: yaml.dump(config, Dumper=yaml.SafeDumper, sort_keys=False))
measure("CSafeDumper", lambda: yaml.dump(config, Dumper=yaml.CSafeDumper, sort_keys=False))
