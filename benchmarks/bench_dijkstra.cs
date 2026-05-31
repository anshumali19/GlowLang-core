
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
