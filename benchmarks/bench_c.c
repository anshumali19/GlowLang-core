#include <stdio.h>
#include <time.h>

double compute_pi(int iterations) {
    double pi = 0.0;
    double divisor = 1.0;
    double sign = 1.0;
    for (int i = 0; i < iterations; i++) {
        pi += sign * (4.0 / divisor);
        divisor += 2.0;
        sign *= -1.0;
    }
    return pi;
}

double matrix_multiply_sim(int size) {
    double total = 0.0;
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            for (int k = 0; k < size; k++) {
                total += (double)(i * j * k) * 0.0001;
            }
        }
    }
    return total;
}

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

long long loop100m() {
    volatile long long s = 0;
    for (int i = 0; i < 100000000; i++) {
        s += i;
    }
    return s;
}

int main() {
    printf("Starting C Benchmarks\n");
    clock_t start;

    start = clock();
    double p = compute_pi(1000000);
    printf("Pi (1M): %fs (Res: %f)\n", (double)(clock() - start) / CLOCKS_PER_SEC, p);

    start = clock();
    double m = matrix_multiply_sim(200);
    printf("Matrix (200^3): %fs (Res: %f)\n", (double)(clock() - start) / CLOCKS_PER_SEC, m);

    start = clock();
    int total_fib = 0;
    for(int i=0; i<30; i++) total_fib += fib(i);
    printf("Fib(30): %fs (Res: %d)\n", (double)(clock() - start) / CLOCKS_PER_SEC, total_fib);

    start = clock();
    long long s = loop100m();
    printf("Loop (100M): %fs (Res: %lld)\n", (double)(clock() - start) / CLOCKS_PER_SEC, s);

    return 0;
}
