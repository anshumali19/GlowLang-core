def fib(n):
    if n < 2: return n
    return fib(n - 1) + fib(n - 2)
total = 0
for i in range(31): total += fib(i)
print(total)
