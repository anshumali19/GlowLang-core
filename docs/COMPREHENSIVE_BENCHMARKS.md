# Comprehensive Industrial Benchmarks (May 2026)

GlowLang (Register-based VM) vs. Industry Standards.

| Benchmark | GlowLang | C | C++ | Go | Java | Python | C# |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Fibonacci** | 0.0037s | 0.0049s | 0.0077s | 0.0077s | 0.0645s | 0.1157s | FAIL | 
| **Loop** | 0.0650s | 0.0036s | 0.0049s | 0.0080s | 0.0321s | 0.1155s | FAIL | 
| **Ackermann** | 0.0043s | 0.0067s | 0.0078s | 0.0163s | 0.0642s | 0.3183s | FAIL | 
| **N-Queens** | 0.0053s | 0.0079s | 0.0079s | 0.0098s | 0.0644s | 0.1162s | FAIL | 
| **Dijkstra** | 0.0037s | 0.0067s | 0.0082s | 0.0159s | 0.0646s | 0.1653s | FAIL | 

*Measurements include runtime startup and execution (average of 3 runs after warmup).*