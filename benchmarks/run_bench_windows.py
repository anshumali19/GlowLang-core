import subprocess
import time
import os
import sys

# Use Windows-compatible paths
GLOWLANG = r"..\bin\glowlang.exe"
LANGUAGES = ["GlowLang", "C", "C++", "Java", "Python"]
BENCHMARKS = ["Fibonacci", "Loop", "Ackermann", "N-Queens", "Dijkstra"]

commands = {
    "Fibonacci": {
        "GlowLang": [GLOWLANG, "bench_fib.glow"],
        "C": [".\\bench_fib_c.exe"],
        "C++": [".\\bench_fib_cpp.exe"],
        "Java": ["java", "BenchFib"],
        "Python": ["python", "bench_fib.py"],
    },
    "Loop": {
        "GlowLang": [GLOWLANG, "bench_loop.glow"],
        "C": [".\\bench_loop_c.exe"],
        "C++": [".\\bench_loop_cpp.exe"],
        "Java": ["java", "BenchLoop"],
        "Python": ["python", "bench_loop.py"],
    },
    "Ackermann": {
        "GlowLang": [GLOWLANG, "dsa_ackermann.glow"],
        "C": [".\\ack_c.exe"],
        "C++": [".\\ack_cpp.exe"],
        "Java": ["java", "DsaAckermann"],
        "Python": ["python", "dsa_ackermann.py"],
    },
    "N-Queens": {
        "GlowLang": [GLOWLANG, "dsa_nqueens.glow"],
        "C": [".\\nq_c.exe"],
        "C++": [".\\nq_cpp.exe"],
        "Java": ["java", "DsaNQueens"],
        "Python": ["python", "dsa_nqueens.py"],
    },
    "Dijkstra": {
        "GlowLang": [GLOWLANG, "bench_dijkstra.glow"],
        "C": [".\\bench_dijkstra_c.exe"],
        "C++": [".\\bench_dijkstra_cpp.exe"],
        "Java": ["java", "DijkstraBench"],
        "Python": ["python", "bench_dijkstra.py"],
    }
}

def run_bench(cmd, cwd):
    try:
        start = time.perf_counter()
        result = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=60)
        elapsed = time.perf_counter() - start
        if result.returncode == 0:
            return elapsed
        return None
    except Exception as e:
        return None

def main():
    bench_dir = os.path.dirname(os.path.abspath(__file__))
    results = {b: {} for b in BENCHMARKS}
    
    print("Starting Benchmarking...")
    for b in BENCHMARKS:
        print(f"\nRunning {b}...")
        for lang in LANGUAGES:
            cmd = commands[b][lang]
            # Warmup
            run_bench(cmd, bench_dir)
            # Run 3 times and average
            times = []
            for _ in range(3):
                t = run_bench(cmd, bench_dir)
                if t:
                    times.append(t)
            if times:
                results[b][lang] = sum(times) / len(times)
                print(f"  {lang:10s}: {results[b][lang]:.4f}s")
            else:
                results[b][lang] = "FAIL"
                print(f"  {lang:10s}: FAIL")

    # Print results table
    print("\n" + "=" * 60)
    print("BENCHMARK RESULTS (average of 3 runs, seconds)")
    print("=" * 60)
    header = "| Benchmark | " + " | ".join(f"{l:>10}" for l in LANGUAGES) + " |"
    sep = "|-----------|" + "|".join(["------------:"] * len(LANGUAGES)) + "|"
    print(header)
    print(sep)
    for b in BENCHMARKS:
        row = f"| {b:9s} | "
        for lang in LANGUAGES:
            val = results[b].get(lang, "N/A")
            if isinstance(val, float):
                row += f"{val:10.4f}s | "
            else:
                row += f"{val:>10} | "
        print(row)

if __name__ == "__main__":
    main()