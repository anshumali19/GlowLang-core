#include <stdio.h>
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    int total = 0;
    for (int i = 0; i <= 30; i++) total += fib(i);
    printf("%d\n", total);
    return 0;
}
