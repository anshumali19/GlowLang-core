# GlowLang Production Benchmark Suite - Engineering Documentation

This document outlines the methodology, execution instructions, and analysis guidance for the GlowLang performance evaluation suite.

## 1. Benchmark Suite Overview

The suite consists of several targeted benchmarks:

- **prod_throughput.glow**: Measures raw computational power using Pi calculation (Leibniz formula) and 3D loop simulation (matrix-like access patterns).
- **prod_jit_warmup.glow**: Analyzes the execution time of a hot function across multiple batches to observe JIT optimization thresholds and stabilization.
- **prod_multicore.glow**: Evaluates multicore scaling and thread overhead by spawning multiple workers performing independent computational tasks.
- **prod_memory_heavy.glow**: Stress-tests memory allocation, heap growth, and list/dictionary scaling by creating large sustained data structures.
- **prod_latency.glow**: Measures tail latency and execution consistency by performing thousands of micro-tasks and reporting outliers.
- **prod_gc_stress.glow**: (Pre-existing) Measures GC efficiency through rapid object tree creation and destruction.
- **prod_graph_dijkstra.glow**: (Pre-existing) Tests algorithm implementation efficiency and list/nested-list performance.
- **prod_startup.glow**: Provides a baseline for engine initialization and teardown overhead.

## 2. Benchmark Execution Instructions

### Prerequisites
- GlowLang binary (usually located at `./bin/glowlang` or `./glowlang`).
- `/usr/bin/time` for memory and startup measurements.

### Manual Execution Steps
1. **Raw Performance**:
   `./bin/glowlang benchmarks/prod_throughput.glow`
2. **JIT Analysis**:
   `./bin/glowlang benchmarks/prod_jit_warmup.glow`
   *Note: Observe if "Batch time" decreases after the first few batches.*
3. **Multicore Scaling**:
   Modify `run_parallel(4, 1000000)` in `benchmarks/prod_multicore.glow` to test with 1, 2, 4, 8, and 16 threads.
4. **Memory Usage**:
   `/usr/bin/time -v ./bin/glowlang benchmarks/prod_memory_heavy.glow`
   *Observe "Maximum resident set size".*
5. **Startup Time**:
   `/usr/bin/time -v ./bin/glowlang benchmarks/prod_startup.glow`
   *Observe "Elapsed (wall clock) time".*

## 3. Measurement Methodology

- **Latency**: Measured using `os.time()` within scripts for internal jitter, and external wall-clock timing for total duration.
- **Throughput**: Calculated as operations per second based on the reported script execution time.
- **Tail Latency**: Focus on p95 and p99 by analyzing the distribution of micro-task timings in `prod_latency.glow`.
- **Heap Growth**: Monitored via step-wise reporting in `prod_memory_heavy.glow`.

## 4. Statistical Analysis Methodology

For rigorous results:
1. **Warmup**: Ignore the first 3 runs of any benchmark to account for OS caching and JIT warmup.
2. **Repetition**: Run each benchmark at least 10 times.
3. **Statistics**: Calculate the Mean, Median, Standard Deviation, and Percentiles (p50, p95, p99, p999).
4. **Outliers**: Remove outliers that are more than 3 standard deviations from the mean only if they can be attributed to external system noise.

## 5. Result Interpretation Guidance

- **High StdDev in Latency**: Indicates GC pauses or thread contention.
- **Batches not getting faster**: Suggests the JIT is not optimizing the specific code path or is already at peak optimization.
- **Sub-linear Multicore Scaling**: Likely due to the Global Interpreter Lock (GIL) or memory bus saturation.
- **Rapid RSS Growth**: Check for memory leaks in the VM internals or inefficient object layout in the language.
