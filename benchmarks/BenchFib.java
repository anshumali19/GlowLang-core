public class BenchFib {
    static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }
    public static void main(String[] args) {
        int total = 0;
        for (int i = 0; i <= 30; i++) total += fib(i);
        System.out.println(total);
    }
}
