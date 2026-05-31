using System;

class Fib {
    static long fib(long n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }

    static void Main() {
        long total = 0;
        for (int i = 0; i < 30; i++) {
            total += fib(i);
        }
        Console.WriteLine(total);
    }
}
