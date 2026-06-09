import time

def compute_pi(iterations):
    pi = 0.0
    divisor = 1.0
    sign = 1.0
    for i in range(iterations):
        pi += sign * (4.0 / divisor)
        divisor += 2.0
        sign *= -1.0
    return pi

def matrix_multiply_sim(size):
    total = 0.0
    for i in range(size):
        for j in range(size):
            for k in range(size):
                total += (i * j * k) * 0.0001
    return total

def fib(n):
    if n < 2: return n
    return fib(n - 1) + fib(n - 2)

def loop100m():
    s = 0
    for i in range(100000000):
        s += i
    return s

print("Starting Python Benchmarks")

start = time.time()
compute_pi(1000000)
print(f"Pi (1M): {time.time() - start:.6f}s")

start = time.time()
matrix_multiply_sim(200)
print(f"Matrix (200^3): {time.time() - start:.6f}s")

start = time.time()
total_fib = 0
for i in range(30): total_fib += fib(i)
print(f"Fib(30): {time.time() - start:.6f}s")

start = time.time()
loop100m()
print(f"Loop (100M): {time.time() - start:.6f}s")
