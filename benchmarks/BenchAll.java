public class BenchAll {
    public static double compute_pi(int iterations) {
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

    public static double matrix_multiply_sim(int size) {
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

    public static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static long loop100m() {
        long s = 0;
        for (int i = 0; i < 100000000; i++) {
            s += i;
        }
        return s;
    }

    public static void main(String[] args) {
        System.out.println("Starting Java Benchmarks");
        long start;

        start = System.nanoTime();
        compute_pi(1000000);
        System.out.printf("Pi (1M): %.6fs\n", (System.nanoTime() - start) / 1e9);

        start = System.nanoTime();
        matrix_multiply_sim(200);
        System.out.printf("Matrix (200^3): %.6fs\n", (System.nanoTime() - start) / 1e9);

        start = System.nanoTime();
        int total_fib = 0;
        for(int i=0; i<30; i++) total_fib += fib(i);
        System.out.printf("Fib(30): %.6fs\n", (System.nanoTime() - start) / 1e9);

        start = System.nanoTime();
        loop100m();
        System.out.printf("Loop (100M): %.6fs\n", (System.nanoTime() - start) / 1e9);
    }
}
