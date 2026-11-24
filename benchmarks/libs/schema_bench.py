# The work of schema_bench.glow with Python's jsonschema library (written in
# Python), its validator built once, collecting every error as
# schema.validate does.
import time
from jsonschema import Draft202012Validator

user = {
    "type": "object",
    "required": ["name", "age", "email"],
    "additionalProperties": False,
    "properties": {
        "name": {"type": "string", "minLength": 1, "maxLength": 50},
        "age": {"type": "integer", "minimum": 0, "maximum": 150},
        "email": {"type": "string", "pattern": "^[^@ ]+@[^@ ]+\\.[a-z]+$"},
        "tags": {"type": "array", "items": {"type": "string"}, "uniqueItems": True},
        "address": {"type": "object", "required": ["city"], "properties": {"city": {"type": "string"}, "zip": {"type": "string", "pattern": "^[0-9]{6}$"}}},
    },
}
good = {"name": "Ana", "age": 30, "email": "ana@glow.example", "tags": ["a", "b", "c"], "address": {"city": "Pune", "zip": "411001"}}
bad = {"name": "", "age": -1, "email": "nope", "tags": ["a", "a"], "address": {"zip": "1"}}
validator = Draft202012Validator(user)
assert len(list(validator.iter_errors(good))) == 0
assert len(list(validator.iter_errors(bad))) == 6

def measure(label, data):
    n = 0
    start = time.time()
    while time.time() - start < 2:
        list(validator.iter_errors(data))
        n += 1
    print(f"{label:<14}{int(n / (time.time() - start)):,} validations/s")

measure("valid", good)
measure("6 problems", bad)
