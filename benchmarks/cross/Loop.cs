using System;

class Loop {
    static void Main() {
        long i = 0;
        long total = 0;
        while (i < 1000000) {
            total += i;
            i++;
        }
        Console.WriteLine(total);
    }
}
