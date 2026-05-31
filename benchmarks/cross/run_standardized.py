
import subprocess
import time
import os

os.chdir("benchmarks/cross")

languages = ["GlowLang", "C", "C++", "Go", "Java", "Python", "C#"]
benchmarks = ["Fibonacci", "Loop", "Ackermann", "N-Queens", "Dijkstra"]

commands = {
    "Fibonacci": {
        "GlowLang": "../../bin/glowlang fib.glow",
        "C": "./fib_c",
        "C++": "./fib_cpp",
        "Go": "./fib_go",
        "Java": "java Fib",
        "Python": "python3 fib.py",
        "C#": "./fib_cs"
    },
    "Loop": {
        "GlowLang": "../../bin/glowlang loop.glow",
        "C": "./loop_c",
        "C++": "./loop_cpp",
        "Go": "./loop_go",
        "Java": "java Loop",
        "Python": "python3 loop.py",
        "C#": "./loop_cs"
    },
    "Ackermann": {
        "GlowLang": "../../bin/glowlang ack.glow",
        "C": "./ack_c",
        "C++": "./ack_cpp",
        "Go": "./ack_go",
        "Java": "java Ack",
        "Python": "python3 ack.py",
        "C#": "./ack_cs"
    },
    "N-Queens": {
        "GlowLang": "../../bin/glowlang nq.glow",
        "C": "./nq_c",
        "C++": "./nq_cpp",
        "Go": "./nq_go",
        "Java": "java NQ",
        "Python": "python3 nq.py",
        "C#": "./nq_cs"
    },
    "Dijkstra": {
        "GlowLang": "../../bin/glowlang dijkstra.glow",
        "C": "./dijkstra_c",
        "C++": "./dijkstra_cpp",
        "Go": "./dijkstra_go",
        "Java": "java Dijkstra",
        "Python": "python3 dijkstra.py",
        "C#": "./dijkstra_cs"
    }
}

def run_bench(cmd):
    start = time.time()
    try:
        # Using shell=True and executing directly if it's a binary
        subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    except Exception as e:
        print(f"Error running {cmd}: {e}")
        return None
    return time.time() - start

results = {b: {} for b in benchmarks}

print("Starting Standardized Benchmarking...")
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

with open("../../docs/COMPREHENSIVE_BENCHMARKS.md", "w") as f:
    f.write("# Comprehensive Industrial Benchmarks (May 2026)\n\n")
    f.write("GlowLang (Register-based VM) vs. Industry Standards.\n\n")
    f.write(markdown_table)
    f.write("\n\n*Measurements include runtime startup and execution (average of 3 runs after warmup).*")
