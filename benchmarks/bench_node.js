const { performance } = require('perf_hooks');

function compute_pi(iterations) {
    let pi = 0.0;
    let divisor = 1.0;
    let sign = 1.0;
    for (let i = 0; i < iterations; i++) {
        pi += sign * (4.0 / divisor);
        divisor += 2.0;
        sign *= -1.0;
    }
    return pi;
}

function matrix_multiply_sim(size) {
    let total = 0.0;
    for (let i = 0; i < size; i++) {
        for (let j = 0; j < size; j++) {
            for (let k = 0; k < size; k++) {
                total += (i * j * k) * 0.0001;
            }
        }
    }
    return total;
}

function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

function loop100m() {
    let s = 0;
    for (let i = 0; i < 100000000; i++) {
        s += i;
    }
    return s;
}

console.log("Starting Node.js Benchmarks");

let start = performance.now();
compute_pi(1000000);
let pi_time = (performance.now() - start) / 1000;
console.log(`Pi (1M): ${pi_time.toFixed(6)}s`);

start = performance.now();
matrix_multiply_sim(200);
let mat_time = (performance.now() - start) / 1000;
console.log(`Matrix (200^3): ${mat_time.toFixed(6)}s`);

start = performance.now();
let total_fib = 0;
for(let i=0; i<30; i++) total_fib += fib(i);
let fib_time = (performance.now() - start) / 1000;
console.log(`Fib(30): ${fib_time.toFixed(6)}s`);

start = performance.now();
loop100m();
let loop_time = (performance.now() - start) / 1000;
console.log(`Loop (100M): ${loop_time.toFixed(6)}s`);
