# Comprehensive Cross-Language Benchmarks

| Benchmark |   GlowLang |          C |        C++ |       Java |     Python |         Go |       Rust |
|-----------|------------:|------------:|------------:|------------:|------------:|------------:|------------:|
| Fibonacci |     0.2976s |     0.0105s |     0.0112s |       FAIL |     0.1993s |     0.0152s |     0.0124s | 
| Loop      |     0.3486s |     0.0063s |     0.0074s |       FAIL |     0.4822s |     0.0129s |     0.0056s | 
| Ackermann |     0.1193s |     0.0090s |     0.0105s |       FAIL |     0.1775s |     0.0133s |     0.0076s | 
| N-Queens  |     0.5986s |     0.0213s |     0.0212s |       FAIL |     0.4971s |     0.0250s |     0.0428s | 
| Dijkstra  |     0.2107s |     0.0085s |     0.0094s |       FAIL |     0.1800s |     0.0168s |     0.0061s | 

*Measurements include runtime startup and execution (average of 3 runs after warmup).*
