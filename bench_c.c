#include <stdio.h>
long long hot_loop(int n) { volatile long long s=0; for(int i=0; i<n; i++) s+=i; return s; }
int main() { printf("%lld\n", hot_loop(100000000)); return 0; }
