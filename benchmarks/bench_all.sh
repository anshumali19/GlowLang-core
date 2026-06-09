#!/bin/bash

run_bench() {
    local name=$1
    local cmd=$2
    printf "%-15s: " "$name"
    { time $cmd > /dev/null 2>&1 ; } 2>&1 | grep real | awk '{print $2}'
}

echo "=== Fibonacci (0-19 sum) ==="
run_bench "GlowLang" "./glowlang bench_fib.glow < /dev/null"
run_bench "Python" "python3 bench_fib.py"
run_bench "C" "./bench_fib_c"
run_bench "C++" "./bench_fib_cpp"
run_bench "Go" "./bench_fib_go"
run_bench "Java" "java BenchFib"

echo ""
echo "=== Loop (100K iterations) ==="
run_bench "GlowLang" "./glowlang bench_loop.glow < /dev/null"
run_bench "Python" "python3 bench_loop.py"
run_bench "C" "./bench_loop_c"
run_bench "C++" "./bench_loop_cpp"
run_bench "Go" "./bench_loop_go"
run_bench "Java" "java BenchLoop"

echo ""
echo "=== Ackermann(3, 8) ==="
# Reducing Ackermann slightly for faster benchmarking across all langs
run_bench "GlowLang" "./glowlang dsa_ackermann.glow < /dev/null"
run_bench "Python" "python3 dsa_ackermann.py"
run_bench "C" "./ack_c"
run_bench "C++" "./ack_cpp"
run_bench "Go" "./ack_go"
run_bench "Java" "java DsaAckermann"

echo ""
echo "=== N-Queens (N=12) ==="
run_bench "GlowLang" "./glowlang dsa_nqueens.glow < /dev/null"
run_bench "Python" "python3 dsa_nqueens.py"
run_bench "C" "./nq_c"
run_bench "C++" "./nq_cpp"
run_bench "Go" "./nq_go"
run_bench "Java" "java DsaNQueens"
