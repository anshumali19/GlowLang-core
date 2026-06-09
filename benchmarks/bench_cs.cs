using System;
using System.Diagnostics;

class Program {
    static double compute_pi(int iterations) {
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

    static double matrix_multiply_sim(int size) {
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

    static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }

    static long loop100m() {
        long s = 0;
        for (int i = 0; i < 100000000; i++) {
            s += i;
        }
        return s;
    }

    static void Main() {
        Console.WriteLine("Starting C# Benchmarks");
        Stopwatch sw = new Stopwatch();

        sw.Start();
        compute_pi(1000000);
        sw.Stop();
        Console.WriteLine($"Pi (1M): {sw.Elapsed.TotalSeconds:F6}s");

        sw.Restart();
        matrix_multiply_sim(200);
        sw.Stop();
        Console.WriteLine($"Matrix (200^3): {sw.Elapsed.TotalSeconds:F6}s");

        sw.Restart();
        int total_fib = 0;
        for(int i=0; i<30; i++) total_fib += fib(i);
        sw.Stop();
        Console.WriteLine($"Fib(30): {sw.Elapsed.TotalSeconds:F6}s");

        sw.Restart();
        loop100m();
        sw.Stop();
        Console.WriteLine($"Loop (100M): {sw.Elapsed.TotalSeconds:F6}s");
    }
}
