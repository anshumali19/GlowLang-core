#!/bin/bash
echo "Compiling C..."
gcc -O3 bench_dijkstra.c -o bench_dijkstra_c
echo "Compiling C++..."
g++ -O3 bench_dijkstra.cpp -o bench_dijkstra_cpp
echo "Compiling Go..."
go build -o bench_dijkstra_go bench_dijkstra.go
echo "Compiling C#..."
mcs bench_dijkstra.cs 2>/dev/null || mcs -out:bench_dijkstra_cs.exe bench_dijkstra.cs 2>/dev/null || csc bench_dijkstra.cs 2>/dev/null
echo "Compiling Java..."
javac DijkstraBench.java

echo "--- RUNNING BENCHMARKS ---"
./bench_dijkstra_c
./bench_dijkstra_cpp
./bench_dijkstra_go
python3 bench_dijkstra.py
mono bench_dijkstra_cs.exe || mono bench_dijkstra.exe || ./bench_dijkstra.exe
java DijkstraBench
../bin/glowlang bench_dijkstra.glow
