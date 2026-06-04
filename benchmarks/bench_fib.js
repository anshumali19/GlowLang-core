function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
let total = 0;
for (let i = 0; i <= 30; i++) total += fib(i);
console.log(total);
