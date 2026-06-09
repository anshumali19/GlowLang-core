#include <stdio.h>

long fib(long n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    long total = 0;
    for (long i = 0; i < 30; i++) {
        total += fib(i);
    }
    printf("%ld\n", total);
    return 0;
}
