# Comprehensive Cross-Language Benchmarks

| Benchmark | GlowLang | C | C++ | Go | Java | Python | C# |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Fibonacci** | 0.2656s | 0.0082s | 0.0079s | 0.0159s | 0.0660s | 0.1821s | 1.1250s | 
| **Loop** | 0.3156s | 0.0038s | 0.0038s | 0.0087s | 0.0656s | 0.4837s | 1.1551s | 
| **Ackermann** | FAIL | 0.0078s | 0.0075s | 0.0212s | 0.0644s | 0.3185s | 1.1690s | 
| **N-Queens** | 0.6278s | 0.0292s | 0.0267s | 0.0326s | 0.0653s | 0.4523s | 1.2046s | 
| **Dijkstra** | 0.2195s | 0.0079s | 0.0078s | 0.0177s | 0.0645s | 0.1667s | 1.1354s | 

*Measurements include runtime startup and execution (average of 3 runs after warmup).*