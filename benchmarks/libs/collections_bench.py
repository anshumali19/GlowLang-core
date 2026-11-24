# The work of collections_bench.glow with Python's heapq, deque and Counter
# (all written in C inside CPython).
import heapq, random, time
from collections import deque, Counter

def rate(label, ops, start):
    print(f"{label:<22}{int(ops / (time.time() - start)):,} ops/s")

random.seed(1)
nums = [random.randint(0, 1000000) for _ in range(200000)]
start = time.time()
h = []
for x in nums:
    heapq.heappush(h, x)
last = -1
while h:
    x = heapq.heappop(h)
    assert x >= last
    last = x
rate("heapq push+pop", 400000, start)

start = time.time()
q = deque()
for i in range(1000):
    q.append(i)
for i in range(1000000):
    q.append(i)
    q.popleft()
rate("deque push+pop", 2000000, start)

vocab = ["alpha", "beta", "gamma", "delta", "eps", "zeta", "eta", "theta", "iota", "kappa"]
words = [random.choice(vocab) for _ in range(1000000)]
start = time.time()
counts = Counter(words)
rate("Counter", 1000000, start)
