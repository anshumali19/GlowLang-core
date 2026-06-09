using System;
class BenchFib {
    static int Fib(int n) {
        if (n < 2) return n;
        return Fib(n - 1) + Fib(n - 2);
    }
    static void Main() {
        int i = 0, total = 0;
        while (i < 20) {
            total += Fib(i);
            i++;
        }
        Console.WriteLine(total);
    }
}
