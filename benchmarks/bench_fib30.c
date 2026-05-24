#include <stdio.h>
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    int i = 0, total = 0;
    while (i < 30) { total += fib(i); i++; }
    printf("%d\n", total);
    return 0;
}
