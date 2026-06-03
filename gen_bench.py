import os

dijkstra_c = """
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define V 1000
#define INF 999999

int minDistance(int dist[], int sptSet[]) {
    int min = INF, min_index = -1;
    for (int v = 0; v < V; v++)
        if (sptSet[v] == 0 && dist[v] <= min)
            min = dist[v], min_index = v;
    return min_index;
}

void dijkstra(int graph[V][V], int src) {
    int *dist = malloc(V * sizeof(int));
    int *sptSet = calloc(V, sizeof(int));
    for (int i = 0; i < V; i++)
        dist[i] = INF;
    dist[src] = 0;
    
    for (int count = 0; count < V - 1; count++) {
        int u = minDistance(dist, sptSet);
        sptSet[u] = 1;
        for (int v = 0; v < V; v++)
            if (!sptSet[v] && graph[u][v] && dist[u] != INF && dist[u] + graph[u][v] < dist[v])
                dist[v] = dist[u] + graph[u][v];
    }
    free(dist);
    free(sptSet);
}

int main() {
    int (*graph)[V] = malloc(sizeof(int[V][V]));
    for (int i = 0; i < V; i++) {
        for (int j = 0; j < V; j++) {
            if (i == j) graph[i][j] = 0;
            else graph[i][j] = (i * j + 1) % 100;
        }
    }
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    dijkstra(graph, 0);
    gettimeofday(&end, NULL);
    
    double time_taken = end.tv_sec + end.tv_usec / 1e6 - start.tv_sec - start.tv_usec / 1e6;
    printf("C Time: %f s\\n", time_taken);
    free(graph);
    return 0;
}
"""

dijkstra_cpp = """
#include <iostream>
#include <vector>
#include <chrono>

using namespace std;

const int V = 1000;
const int INF = 999999;

int minDistance(const vector<int>& dist, const vector<bool>& sptSet) {
    int min = INF, min_index = -1;
    for (int v = 0; v < V; v++)
        if (!sptSet[v] && dist[v] <= min) {
            min = dist[v];
            min_index = v;
        }
    return min_index;
}

void dijkstra(const vector<vector<int>>& graph, int src) {
    vector<int> dist(V, INF);
    vector<bool> sptSet(V, false);
    dist[src] = 0;
    
    for (int count = 0; count < V - 1; count++) {
        int u = minDistance(dist, sptSet);
        sptSet[u] = true;
        for (int v = 0; v < V; v++)
            if (!sptSet[v] && graph[u][v] && dist[u] != INF && dist[u] + graph[u][v] < dist[v])
                dist[v] = dist[u] + graph[u][v];
    }
}

int main() {
    vector<vector<int>> graph(V, vector<int>(V));
    for (int i = 0; i < V; i++) {
        for (int j = 0; j < V; j++) {
            if (i == j) graph[i][j] = 0;
            else graph[i][j] = (i * j + 1) % 100;
        }
    }
    
    auto start = chrono::high_resolution_clock::now();
    dijkstra(graph, 0);
    auto end = chrono::high_resolution_clock::now();
    
    chrono::duration<double> time_taken = end - start;
    cout << "C++ Time: " << time_taken.count() << " s\\n";
    return 0;
}
"""

dijkstra_go = """
package main

import (
	"fmt"
	"time"
)

const V = 1000
const INF = 999999

func minDistance(dist []int, sptSet []bool) int {
	min := INF
	min_index := -1
	for v := 0; v < V; v++ {
		if !sptSet[v] && dist[v] <= min {
			min = dist[v]
			min_index = v
		}
	}
	return min_index
}

func dijkstra(graph [][]int, src int) {
	dist := make([]int, V)
	sptSet := make([]bool, V)
	for i := 0; i < V; i++ {
		dist[i] = INF
	}
	dist[src] = 0

	for count := 0; count < V-1; count++ {
		u := minDistance(dist, sptSet)
		sptSet[u] = true
		for v := 0; v < V; v++ {
			if !sptSet[v] && graph[u][v] != 0 && dist[u] != INF && dist[u]+graph[u][v] < dist[v] {
				dist[v] = dist[u] + graph[u][v]
			}
		}
	}
}

func main() {
	graph := make([][]int, V)
	for i := 0; i < V; i++ {
		graph[i] = make([]int, V)
		for j := 0; j < V; j++ {
			if i == j {
				graph[i][j] = 0
			} else {
				graph[i][j] = (i*j + 1) % 100
			}
		}
	}

	start := time.Now()
	dijkstra(graph, 0)
	elapsed := time.Since(start)

	fmt.Printf("Go Time: %f s\\n", elapsed.Seconds())
}
"""

dijkstra_py = """
import time

V = 1000
INF = 999999

def minDistance(dist, sptSet):
    min_val = INF
    min_index = -1
    for v in range(V):
        if not sptSet[v] and dist[v] <= min_val:
            min_val = dist[v]
            min_index = v
    return min_index

def dijkstra(graph, src):
    dist = [INF] * V
    sptSet = [False] * V
    dist[src] = 0

    for _ in range(V - 1):
        u = minDistance(dist, sptSet)
        sptSet[u] = True
        for v in range(V):
            if not sptSet[v] and graph[u][v] and dist[u] != INF and dist[u] + graph[u][v] < dist[v]:
                dist[v] = dist[u] + graph[u][v]

graph = [[0]*V for _ in range(V)]
for i in range(V):
    for j in range(V):
        if i == j:
            graph[i][j] = 0
        else:
            graph[i][j] = (i * j + 1) % 100

start = time.time()
dijkstra(graph, 0)
end = time.time()

print(f"Python Time: {end - start:.6f} s")
"""

dijkstra_cs = """
using System;
using System.Diagnostics;

class DijkstraBench {
    const int V = 1000;
    const int INF = 999999;

    static int minDistance(int[] dist, bool[] sptSet) {
        int min = INF, min_index = -1;
        for (int v = 0; v < V; v++)
            if (!sptSet[v] && dist[v] <= min) {
                min = dist[v];
                min_index = v;
            }
        return min_index;
    }

    static void dijkstra(int[,] graph, int src) {
        int[] dist = new int[V];
        bool[] sptSet = new bool[V];
        for (int i = 0; i < V; i++) dist[i] = INF;
        dist[src] = 0;

        for (int count = 0; count < V - 1; count++) {
            int u = minDistance(dist, sptSet);
            sptSet[u] = true;
            for (int v = 0; v < V; v++)
                if (!sptSet[v] && graph[u, v] != 0 && dist[u] != INF && dist[u] + graph[u, v] < dist[v])
                    dist[v] = dist[u] + graph[u, v];
        }
    }

    static void Main() {
        int[,] graph = new int[V, V];
        for (int i = 0; i < V; i++) {
            for (int j = 0; j < V; j++) {
                if (i == j) graph[i, j] = 0;
                else graph[i, j] = (i * j + 1) % 100;
            }
        }

        Stopwatch sw = Stopwatch.StartNew();
        dijkstra(graph, 0);
        sw.Stop();

        Console.WriteLine($"C# Time: {sw.Elapsed.TotalSeconds} s");
    }
}
"""

dijkstra_java = """
public class DijkstraBench {
    static final int V = 1000;
    static final int INF = 999999;

    static int minDistance(int[] dist, boolean[] sptSet) {
        int min = INF, min_index = -1;
        for (int v = 0; v < V; v++)
            if (!sptSet[v] && dist[v] <= min) {
                min = dist[v];
                min_index = v;
            }
        return min_index;
    }

    static void dijkstra(int[][] graph, int src) {
        int[] dist = new int[V];
        boolean[] sptSet = new boolean[V];
        for (int i = 0; i < V; i++) dist[i] = INF;
        dist[src] = 0;

        for (int count = 0; count < V - 1; count++) {
            int u = minDistance(dist, sptSet);
            sptSet[u] = true;
            for (int v = 0; v < V; v++)
                if (!sptSet[v] && graph[u][v] != 0 && dist[u] != INF && dist[u] + graph[u][v] < dist[v])
                    dist[v] = dist[u] + graph[u][v];
        }
    }

    public static void main(String[] args) {
        int[][] graph = new int[V][V];
        for (int i = 0; i < V; i++) {
            for (int j = 0; j < V; j++) {
                if (i == j) graph[i][j] = 0;
                else graph[i][j] = (i * j + 1) % 100;
            }
        }

        long start = System.nanoTime();
        dijkstra(graph, 0);
        long end = System.nanoTime();

        System.out.printf("Java Time: %f s\\n", (end - start) / 1e9);
    }
}
"""

dijkstra_glow = """
func minDistance(dist, sptSet, V):
    let min_val = 999999
    let min_index = 0 - 1
    let v = 0
    while v < V:
        if sptSet[v] == 0:
            if dist[v] <= min_val:
                min_val = dist[v]
                min_index = v
            end
        end
        v = v + 1
    end
    return min_index
end

func dijkstra(graph, V, src):
    let dist = range(V)
    let sptSet = range(V)
    let i = 0
    while i < V:
        dist[i] = 999999
        sptSet[i] = 0
        i = i + 1
    end
    dist[src] = 0
    
    let count = 0
    while count < V - 1:
        let u = minDistance(dist, sptSet, V)
        sptSet[u] = 1
        
        let v = 0
        let row = graph[u]
        while v < V:
            if sptSet[v] == 0:
                if row[v] != 0:
                    if dist[u] != 999999:
                        if dist[u] + row[v] < dist[v]:
                            dist[v] = dist[u] + row[v]
                        end
                    end
                end
            end
            v = v + 1
        end
        count = count + 1
    end
end

let V = 1000
let graph = range(V)
let i = 0
while i < V:
    let row = range(V)
    let j = 0
    while j < V:
        if i == j:
            row[j] = 0
        end
        if i != j:
            row[j] = (i * j + 1) % 100
        end
        j = j + 1
    end
    graph[i] = row
    i = i + 1
end

let start = os.time()
let res = dijkstra(graph, V, 0)
let end_time = os.time()
show "Glow Time: ", end_time - start, " s (Result: ", res, ")"
"""

run_sh = """#!/bin/bash
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
"""

import os
os.makedirs("benchmarks", exist_ok=True)
with open("benchmarks/bench_dijkstra.c", "w") as f: f.write(dijkstra_c)
with open("benchmarks/bench_dijkstra.cpp", "w") as f: f.write(dijkstra_cpp)
with open("benchmarks/bench_dijkstra.go", "w") as f: f.write(dijkstra_go)
with open("benchmarks/bench_dijkstra.py", "w") as f: f.write(dijkstra_py)
with open("benchmarks/bench_dijkstra.cs", "w") as f: f.write(dijkstra_cs)
with open("benchmarks/DijkstraBench.java", "w") as f: f.write(dijkstra_java)
with open("benchmarks/bench_dijkstra.glow", "w") as f: f.write(dijkstra_glow)
with open("benchmarks/run_complex_bench.sh", "w") as f: f.write(run_sh)

os.chmod("benchmarks/run_complex_bench.sh", 0o755)
