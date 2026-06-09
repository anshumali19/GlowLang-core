#!/bin/bash

run_bench() {
    local name=$1
    local cmd=$2
    printf "%-15s: " "$name"
    { time $cmd > /dev/null 2>&1 ; } 2>&1 | grep real | awk '{print $2}'
}

echo "=== Fibonacci (30) ==="
run_bench "GlowLang" "./glowlang bench_fib30.glow < /dev/null"
run_bench "Python" "python3 bench_fib30.py"
run_bench "C" "./bench_fib30_c"
# C++ and Java missing for fib30 specifically, skipping or creating

echo ""
echo "=== Loop (10M iterations) ==="
run_bench "GlowLang" "./glowlang bench_loop10m.glow < /dev/null"
run_bench "Python" "python3 bench_loop10m.py"
run_bench "C" "./bench_loop10m_c"

echo ""
echo "=== Ackermann (3, 11) ==="
run_bench "GlowLang" "./glowlang dsa_ackermann.glow < /dev/null"
run_bench "Python" "python3 dsa_ackermann.py"
run_bench "C" "./ack_c"
run_bench "Go" "./ack_go"
