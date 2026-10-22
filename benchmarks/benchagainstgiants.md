# Comprehensive Cross-Language Benchmarks

| Benchmark |   GlowLang |          C |        C++ |       Java |     Python |         Go |       Rust |
|-----------|------------:|------------:|------------:|------------:|------------:|------------:|------------:|
| Fibonacci |     0.2115s |     0.0089s |     0.0098s |     0.0888s |     0.1845s |     0.0141s |     0.0092s | 
| Loop      |     0.2247s |     0.0059s |     0.0081s |     0.0612s |     0.4553s |     0.0122s |     0.0049s | 
| Ackermann |     0.1006s |     0.0075s |     0.0096s |     0.0772s |     0.1735s |     0.0148s |     0.0154s | 
| N-Queens  |     0.4862s |     0.0193s |     0.0193s |     0.1018s |     0.4834s |     0.0262s |     0.0401s | 
| Dijkstra  |     0.1603s |     0.0073s |     0.0082s |     0.0935s |     0.1681s |     0.0184s |     0.0080s | 

*Measurements include full runtime process startup, AST parsing, bytecode compilation, JIT optimization, and execution (average of 3 runs after warmup on modern x86_64).*
*Pure function execution with monomorphic inline caching and JIT: Fibonacci(30) runs in 14.54ms (16.5x faster than CPython).*
