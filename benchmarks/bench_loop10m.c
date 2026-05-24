#include <stdio.h>
int main() {
    int i = 0;
    int total = 0;
    while (i < 10000000) { total += i; i++; }
    printf("%d\n", total);
    return 0;
}
