public class Loop {
    public static void main(String[] args) {
        long i = 0;
        long total = 0;
        while (i < 1000000) {
            total += i;
            i++;
        }
        System.out.println(total);
    }
}
