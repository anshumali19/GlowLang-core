using System;
class BenchLoop {
    static void Main() {
        int i = 0, total = 0;
        while (i < 100000) {
            total += i;
            i++;
        }
        Console.WriteLine(total);
    }
}
