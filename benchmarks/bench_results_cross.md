# Comprehensive Cross-Language Benchmarks

| Benchmark | GlowLang | C | C++ | Go | Java | Python | C# |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Fibonacci** | 0.2660s | 0.0037s | 0.0035s | 0.0035s | 0.0646s | 0.0157s | 1.3070s | 
| **Loop** | 0.0111s | 0.0040s | 0.0064s | 0.0039s | 0.0647s | 0.0327s | 1.2725s | 
| **Ackermann** | 2.9346s | 0.1675s | 0.1651s | 0.9370s | 0.6542s | FAIL | 1.2750s | 
| **N-Queens** | 0.1717s | 0.0327s | 0.0282s | 0.0332s | 0.0651s | 0.5212s | 1.5756s | 
| **Dijkstra** | 0.0646s | 0.0081s | 0.0079s | 0.0163s | 0.1153s | 0.2005s | 1.2888s | 

*Measurements include runtime startup and execution (average of 3 runs after warmup).*