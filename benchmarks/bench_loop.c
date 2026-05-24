#include <stdio.h>

int main() {
    int i = 0, total = 0;
    while (i < 100000) {
        total += i;
        i++;
    }
    printf("%d\n", total);
    return 0;
}
