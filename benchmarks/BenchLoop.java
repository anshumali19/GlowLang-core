public class BenchLoop {
    public static void main(String[] args) {
        long total = 0;
        for (int i = 0; i < 10000000; i++) total += i;
        System.out.println(total);
    }
}
