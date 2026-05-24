def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

i = 0
total = 0
while i < 20:
    total = total + fib(i)
    i = i + 1
print(total)
