#include <stdio.h>

int main() {
    long i = 0;
    long total = 0;
    while (i < 1000000) {
        total += i;
        i++;
    }
    printf("%ld\n", total);
    return 0;
}
