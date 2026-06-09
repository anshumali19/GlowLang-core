#include <iostream>

int main() {
    long i = 0;
    long total = 0;
    while (i < 1000000) {
        total += i;
        i++;
    }
    std::cout << total << std::endl;
    return 0;
}
