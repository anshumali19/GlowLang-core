import subprocess
import time
import os
import statistics
import json

GLOW_BIN = "./bin/glowlang"

def measure_execution(script_path, iterations=100):
    timings = []
    mem_usage = []
    
    for _ in range(iterations):
        start_time = time.perf_counter()
        # Use /usr/bin/time -v to get RSS if possible, or just measure process end
        process = subprocess.Popen([GLOW_BIN, script_path], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        _, stderr = process.communicate()
        end_time = time.perf_counter()
        
        if process.returncode != 0:
            print(f"Error running {script_path}: {stderr.decode()}")
            continue
            
        timings.append((end_time - start_time) * 1000) # ms
        
    if not timings:
        return None
        
    timings.sort()
    results = {
        "p50": statistics.median(timings),
        "p95": timings[int(len(timings) * 0.95)],
        "p99": timings[int(len(timings) * 0.99)],
        "p999": timings[int(len(timings) * 0.999)] if len(timings) >= 1000 else timings[-1],
        "avg": statistics.mean(timings),
        "stddev": statistics.stdev(timings) if len(timings) > 1 else 0
    }
    return results

def measure_memory_and_startup(script_path, iterations=10):
    # Use /usr/bin/time -v to get 'Maximum resident set size'
    rss_values = []
    startup_times = []
    
    for _ in range(iterations):
        # We run a very small script for startup, or just the main script
        start = time.perf_counter()
        # Use shell to get /usr/bin/time output
        cmd = f"/usr/bin/time -v {GLOW_BIN} {script_path} 2>&1"
        output = subprocess.check_output(cmd, shell=True).decode()
        end = time.perf_counter()
        
        startup_times.append((end - start) * 1000)
        
        for line in output.split('\n'):
            if "Maximum resident set size" in line:
                rss = int(line.split(':')[-1].strip())
                rss_values.append(rss)
                break
                
    return {
        "avg_rss_kb": statistics.mean(rss_values) if rss_values else 0,
        "max_rss_kb": max(rss_values) if rss_values else 0,
        "avg_startup_ms": statistics.mean(startup_times) if startup_times else 0
    }

def run_benchmarks():
    benchmarks = [
        ("Arithmetic Loop", "benchmarks/bench_loop.glow"),
        ("Fibonacci (Recursive)", "benchmarks/bench_fib.glow"),
        ("Dijkstra (Graph)", "benchmarks/bench_dijkstra.glow"),
        ("GC Stress (Objects)", "benchmarks/prod_gc_stress.glow")
    ]
    
    all_results = {}
    
    for name, path in benchmarks:
        print(f"Benchmarking {name}...")
        if not os.path.exists(path):
            print(f"Skipping {name}, path not found: {path}")
            continue
            
        latency = measure_execution(path, iterations=50) # 50 for speed in this environment
        mem_startup = measure_memory_and_startup(path, iterations=5)
        
        all_results[name] = {
            "latency": latency,
            "memory_startup": mem_startup
        }
        
    with open("bench_results.json", "w") as f:
        json.dump(all_results, f, indent=4)
    
    print("Benchmarking complete. Results saved to bench_results.json")

if __name__ == "__main__":
    run_benchmarks()
