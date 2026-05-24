public class BenchLoop {
    public static void main(String[] args) {
        int i = 0, total = 0;
        while (i < 100000) {
            total += i;
            i++;
        }
        System.out.println(total);
    }
}
