using System;

class Dijkstra {
    private const int V = 1000;
    private const int INF = 999999;

    private static int MinDistance(int[] dist, bool[] sptSet) {
        int min = INF, min_index = -1;
        for (int v = 0; v < V; v++) {
            if (!sptSet[v] && dist[v] <= min) {
                min = dist[v];
                min_index = v;
            }
        }
        return min_index;
    }

    private static void RunDijkstra(int[][] graph, int src) {
        int[] dist = new int[V];
        bool[] sptSet = new bool[V];

        for (int i = 0; i < V; i++) {
            dist[i] = INF;
            sptSet[i] = false;
        }

        dist[src] = 0;

        for (int count = 0; count < V - 1; count++) {
            int u = MinDistance(dist, sptSet);
            if (u == -1) break;
            sptSet[u] = true;
            for (int v = 0; v < V; v++) {
                if (!sptSet[v] && graph[u][v] != 0 && dist[u] != INF && dist[u] + graph[u][v] < dist[v]) {
                    dist[v] = dist[u] + graph[u][v];
                }
            }
        }
    }

    static void Main() {
        int[][] graph = new int[V][];
        for (int i = 0; i < V; i++) {
            graph[i] = new int[V];
            for (int j = 0; j < V; j++) {
                if (i == j) graph[i][j] = 0;
                else graph[i][j] = (i * j + 1) % 100;
            }
        }

        RunDijkstra(graph, 0);
        Console.WriteLine("Done");
    }
}
