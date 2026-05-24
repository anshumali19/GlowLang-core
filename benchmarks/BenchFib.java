public class BenchFib {
    static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }
    public static void main(String[] args) {
        int i = 0, total = 0;
        while (i < 20) {
            total += fib(i);
            i++;
        }
        System.out.println(total);
    }
}
