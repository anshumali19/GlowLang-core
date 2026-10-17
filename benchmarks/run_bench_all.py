import subprocess
import time
import os
import sys

GLOWLANG = os.path.abspath(os.path.join(os.path.dirname(__file__), r"..\bin\glowlang.exe"))
LANGUAGES = ["GlowLang", "C", "C++", "Java", "Python", "Go", "Rust"]
BENCHMARKS = ["Fibonacci", "Loop", "Ackermann", "N-Queens", "Dijkstra"]

commands = {
    "Fibonacci": {
        "GlowLang": [GLOWLANG, "bench_fib.glow"],
        "C": [".\\bench_fib_c.exe"],
        "C++": [".\\bench_fib_cpp.exe"],
        "Java": ["java", "BenchFib"],
        "Python": ["python", "bench_fib.py"],
        "Go": [".\\bench_fib_go.exe"],
        "Rust": [".\\bench_fib_rust.exe"],
    },
    "Loop": {
        "GlowLang": [GLOWLANG, "bench_loop.glow"],
        "C": [".\\bench_loop_c.exe"],
        "C++": [".\\bench_loop_cpp.exe"],
        "Java": ["java", "BenchLoop"],
        "Python": ["python", "bench_loop.py"],
        "Go": [".\\bench_loop_go.exe"],
        "Rust": [".\\bench_loop_rust.exe"],
    },
    "Ackermann": {
        "GlowLang": [GLOWLANG, "dsa_ackermann.glow"],
        "C": [".\\ack_c.exe"],
        "C++": [".\\ack_cpp.exe"],
        "Java": ["java", "DsaAckermann"],
        "Python": ["python", "dsa_ackermann.py"],
        "Go": [".\\dsa_ackermann_go.exe"],
        "Rust": [".\\dsa_ackermann_rust.exe"],
    },
    "N-Queens": {
        "GlowLang": [GLOWLANG, "dsa_nqueens.glow"],
        "C": [".\\nq_c.exe"],
        "C++": [".\\nq_cpp.exe"],
        "Java": ["java", "DsaNQueens"],
        "Python": ["python", "dsa_nqueens.py"],
        "Go": [".\\dsa_nqueens_go.exe"],
        "Rust": [".\\dsa_nqueens_rust.exe"],
    },
    "Dijkstra": {
        "GlowLang": [GLOWLANG, "bench_dijkstra.glow"],
        "C": [".\\bench_dijkstra_c.exe"],
        "C++": [".\\bench_dijkstra_cpp.exe"],
        "Java": ["java", "DijkstraBench"],
        "Python": ["python", "bench_dijkstra.py"],
        "Go": [".\\bench_dijkstra_go.exe"],
        "Rust": [".\\bench_dijkstra_rust.exe"],
    }
}

def run_bench(cmd, cwd):
    try:
        resolved_cmd = list(cmd)
        if resolved_cmd[0].startswith(('.\\', './')):
            resolved_cmd[0] = os.path.abspath(os.path.join(cwd, resolved_cmd[0]))
        start = time.perf_counter()
        result = subprocess.run(resolved_cmd, cwd=cwd, capture_output=True, timeout=120)
        elapsed = time.perf_counter() - start
        if result.returncode == 0:
            return elapsed
        return None
    except Exception:
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
    print("\n" + "=" * 80)
    print("BENCHMARK RESULTS (average of 3 runs, seconds)")
    print("=" * 80)
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

    # Write markdown
    with open("benchagainstgiants.md", "w") as f:
        f.write("# Comprehensive Cross-Language Benchmarks\n\n")
        f.write(header + "\n")
        f.write(sep + "\n")
        for b in BENCHMARKS:
            row = f"| {b:9s} | "
            for lang in LANGUAGES:
                val = results[b].get(lang, "N/A")
                if isinstance(val, float):
                    row += f"{val:10.4f}s | "
                else:
                    row += f"{val:>10} | "
            f.write(row + "\n")
        f.write("\n*Measurements include runtime startup and execution (average of 3 runs after warmup).*\n")

if __name__ == "__main__":
    main()