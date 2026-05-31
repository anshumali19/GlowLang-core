#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

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
    std::cout << "Starting C++ Benchmarks" << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    start = std::chrono::high_resolution_clock::now();
    double p = compute_pi(1000000);
    std::cout << "Pi (1M): " << std::fixed << std::setprecision(6) << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "s (Res: " << p << ")" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    double m = matrix_multiply_sim(200);
    std::cout << "Matrix (200^3): " << std::fixed << std::setprecision(6) << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "s (Res: " << m << ")" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    int total_fib = 0;
    for(int i=0; i<30; i++) total_fib += fib(i);
    std::cout << "Fib(30): " << std::fixed << std::setprecision(6) << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "s (Res: " << total_fib << ")" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    long long s = loop100m();
    std::cout << "Loop (100M): " << std::fixed << std::setprecision(6) << std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() << "s (Res: " << s << ")" << std::endl;

    return 0;
}
