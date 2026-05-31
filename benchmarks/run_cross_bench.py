
import subprocess
import time
import os

languages = ["GlowLang", "C", "C++", "Go", "Java", "Python", "C#"]
benchmarks = ["Fibonacci", "Loop", "Ackermann", "N-Queens", "Dijkstra"]

commands = {
    "Fibonacci": {
        "GlowLang": "../bin/glowlang bench_fib.glow",
        "C": "./bench_fib_c",
        "C++": "./bench_fib_cpp",
        "Go": "./bench_fib_go",
        "Java": "java BenchFib",
        "Python": "python3 bench_fib.py",
        "C#": "dotnet run --project CSharpBench -- fib"
    },
    "Loop": {
        "GlowLang": "../bin/glowlang bench_loop.glow",
        "C": "./bench_loop_c",
        "C++": "./bench_loop_cpp",
        "Go": "./bench_loop_go",
        "Java": "java BenchLoop",
        "Python": "python3 bench_loop.py",
        "C#": "dotnet run --project CSharpBench -- loop"
    },
    "Ackermann": {
        "GlowLang": "../bin/glowlang dsa_ackermann.glow",
        "C": "./ack_c",
        "C++": "./ack_cpp",
        "Go": "./ack_go",
        "Java": "java DsaAckermann",
        "Python": "python3 dsa_ackermann.py",
        "C#": "dotnet run --project CSharpBench -- ack"
    },
    "N-Queens": {
        "GlowLang": "../bin/glowlang dsa_nqueens.glow",
        "C": "./nq_c",
        "C++": "./nq_cpp",
        "Go": "./nq_go",
        "Java": "java DsaNQueens",
        "Python": "python3 dsa_nqueens.py",
        "C#": "dotnet run --project CSharpBench -- nqueens"
    },
    "Dijkstra": {
        "GlowLang": "../bin/glowlang bench_dijkstra.glow",
        "C": "./bench_dijkstra_c",
        "C++": "./bench_dijkstra_cpp",
        "Go": "./bench_dijkstra_go",
        "Java": "java DijkstraBench",
        "Python": "python3 bench_dijkstra.py",
        "C#": "dotnet run --project CSharpBench -- dijkstra"
    }
}

def run_bench(cmd):
    start = time.time()
    try:
        subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    except:
        return None
    return time.time() - start

results = {b: {} for b in benchmarks}

print("Starting Benchmarking...")
for b in benchmarks:
    print(f"Running {b}...")
    for lang in languages:
        cmd = commands[b][lang]
        # Warmup
        run_bench(cmd)
        # Run 3 times and take average
        times = []
        for _ in range(3):
            t = run_bench(cmd)
            if t is not None:
                times.append(t)
        if times:
            results[b][lang] = sum(times) / len(times)
        else:
            results[b][lang] = "FAIL"

# Generate Table
header = "| Benchmark | " + " | ".join(languages) + " |"
separator = "| :--- | " + " | ".join([":---:"] * len(languages)) + " |"
rows = []
for b in benchmarks:
    row = f"| **{b}** | "
    for lang in languages:
        val = results[b].get(lang, "N/A")
        if isinstance(val, float):
            row += f"{val:.4f}s | "
        else:
            row += f"{val} | "
    rows.append(row)

markdown_table = "\n".join([header, separator] + rows)
print("\nResults Table:")
print(markdown_table)

with open("bench_results_cross.md", "w") as f:
    f.write("# Comprehensive Cross-Language Benchmarks\n\n")
    f.write(markdown_table)
    f.write("\n\n*Measurements include runtime startup and execution (average of 3 runs after warmup).*")
