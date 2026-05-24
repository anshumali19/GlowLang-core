#include <iostream>
using namespace std;

int main() {
    int i = 0, total = 0;
    while (i < 100000) {
        total += i;
        i++;
    }
    cout << total << endl;
    return 0;
}
