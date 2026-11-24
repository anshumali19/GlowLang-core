# The work of lru_bench.glow with the usual Python LRU cache: an OrderedDict
# (written in C) with move_to_end and popitem.
import random, time
from collections import OrderedDict

class Cache:
    def __init__(self, max_items):
        self.max_items = max_items
        self.items = OrderedDict()

    def get(self, key):
        value = self.items.get(key)
        if value is not None:
            self.items.move_to_end(key)
        return value

    def set(self, key, value):
        self.items[key] = value
        self.items.move_to_end(key)
        if len(self.items) > self.max_items:
            self.items.popitem(last=False)

random.seed(5)
keys = ["k" + str(random.randint(1, 2000)) for _ in range(1000000)]
c = Cache(1000)
start = time.time()
for k in keys:
    if c.get(k) is None:
        c.set(k, k)
print(f"{'OrderedDict':<14}{int(1000000 / (time.time() - start)):,} lookups/s")
