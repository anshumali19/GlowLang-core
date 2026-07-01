import subprocess
import time
import json
import os

languages = ["GlowLang", "C", "C++", "Go", "Node.js", "Python 3", "Perl"]
benchmarks = ["Fibonacci", "Loop", "Ackermann", "N-Queens", "Dijkstra"]

commands = {
    "Fibonacci": {
        "GlowLang": "../bin/glowlang bench_fib.glow",
        "C": "./bench_fib_c",
        "C++": "./bench_fib_cpp",
        "Go": "./bench_fib_go",
        "Node.js": "node bench_fib.js",
        "Python 3": "python3 bench_fib.py",
        "Perl": "perl bench_fib.pl"
    },
    "Loop": {
        "GlowLang": "../bin/glowlang bench_loop.glow",
        "C": "./bench_loop_c",
        "C++": "./bench_loop_cpp",
        "Go": "./bench_loop_go",
        "Node.js": "node bench_loop.js",
        "Python 3": "python3 bench_loop.py",
        "Perl": "perl bench_loop.pl"
    },
    "Ackermann": {
        "GlowLang": "../bin/glowlang dsa_ackermann.glow",
        "C": "./ack_c",
        "C++": "./ack_cpp",
        "Go": "./ack_go",
        "Node.js": "node dsa_ackermann.js",
        "Python 3": "python3 dsa_ackermann.py",
        "Perl": "perl dsa_ackermann.pl"
    },
    "N-Queens": {
        "GlowLang": "../bin/glowlang dsa_nqueens_simple.glow",
        "C": "./nq_c",
        "C++": "./nq_cpp",
        "Go": "./nq_go",
        "Node.js": "node dsa_nqueens.js",
        "Python 3": "python3 dsa_nqueens.py",
        "Perl": "perl dsa_nqueens.pl"
    },
    "Dijkstra": {
        "GlowLang": "../bin/glowlang bench_dijkstra.glow",
        "C": "./bench_dijkstra_c",
        "C++": "./bench_dijkstra_cpp",
        "Go": "./bench_dijkstra_go",
        "Node.js": "node bench_dijkstra.js",
        "Python 3": "python3 bench_dijkstra.py",
        "Perl": "perl bench_dijkstra.pl"
    }
}

def run_bench(cmd):
    start = time.time()
    try:
        res = subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    except Exception as e:
        return None
    return time.time() - start

results = {b: {} for b in benchmarks}

print("Starting Comprehensive Benchmarking...")
for b in benchmarks:
    print(f"Running {b}...")
    for lang in languages:
        cmd = commands[b][lang]
        # Warmup
        w = run_bench(cmd)
        if w is None:
            results[b][lang] = "FAIL"
            print(f"  {lang}: FAIL")
            continue
        # Run 3 times
        times = []
        for _ in range(3):
            t = run_bench(cmd)
            if t is not None:
                times.append(t)
        if times:
            avg = sum(times) / len(times)
            results[b][lang] = avg
            print(f"  {lang}: {avg:.4f}s")
        else:
            results[b][lang] = "FAIL"
            print(f"  {lang}: FAIL")

with open("comprehensive_results.json", "w") as f:
    json.dump(results, f, indent=2)

print("\nSaved comprehensive_results.json")
