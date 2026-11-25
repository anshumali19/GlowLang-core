# The work of ini_bench.glow with Python's configparser (written in Python),
# keys kept in their case as ini.parse keeps them.
import configparser, time

text = open("benchmarks/libs/ini_config.ini", encoding="utf-8").read()

def parse():
    c = configparser.ConfigParser(interpolation=None)
    c.optionxform = str
    c.read_string(text)
    return c

assert parse()["mail"]["note_3"] == "first line of a longer note\ncontinued on a second line"
n = 0
start = time.time()
while time.time() - start < 2:
    parse()
    n += 1
print(f"{'configparser':<20}{int(n / (time.time() - start)):,} parses/s")
