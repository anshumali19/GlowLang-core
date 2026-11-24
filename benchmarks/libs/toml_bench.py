# The work of toml_bench.glow with Python's tomllib (written in Python).
import time, tomllib

text = open("benchmarks/libs/toml_config.toml", encoding="utf-8").read()
assert tomllib.loads(text)["routes"][2]["rate"]["burst"] == 20
n = 0
start = time.time()
while time.time() - start < 2:
    tomllib.loads(text)
    n += 1
print(f"{'tomllib.loads':<16}{int(n / (time.time() - start)):,} parses/s")
