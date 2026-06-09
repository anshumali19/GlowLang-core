import subprocess
import time
import os

def run_command(cmd):
    print(f"Running: {cmd}")
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error: {result.stderr}")
        return result.stdout.strip(), result.stderr.strip()
    except Exception as e:
        print(f"Exception: {e}")
        return "", str(e)

# 1. Compile C
run_command("gcc benchmarks/dsa_nqueens.c -o benchmarks/nq_c -O3 -lm")

# 2. Compile C++
run_command("g++ benchmarks/dsa_nqueens.cpp -o benchmarks/nq_cpp -O3")

# 3. Compile Go
run_command("go build -o benchmarks/nq_go benchmarks/dsa_nqueens.go")

# 4. Compile C#
mcs_path = run_command("which mcs")[0]
if mcs_path:
    run_command("mcs benchmarks/dsa_nqueens.cs -out:benchmarks/nq_cs.exe")
else:
    print("mcs not found, skipping C# compilation")

# 5. Run benchmarks
benchmarks = [
    ("GlowLang", "bin/glowlang benchmarks/dsa_nqueens.glow"),
    ("C", "benchmarks/nq_c"),
    ("C++", "benchmarks/nq_cpp"),
    ("Go", "benchmarks/nq_go"),
    ("Python", "python3 benchmarks/dsa_nqueens.py")
]

if mcs_path:
    benchmarks.append(("C# (Mono)", "mono benchmarks/nq_cs.exe"))

results = []
for name, cmd in benchmarks:
    print(f"Benchmarking {name}...")
    stdout, stderr = run_command(cmd)
    results.append((name, stdout))

print("\n--- Performance Report ---")
for name, output in results:
    print(f"{name}: {output}")
