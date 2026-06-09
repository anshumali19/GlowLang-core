public class BenchDijkstra {
    static final int V = 1000;
    static final int INF = 999999;

    static int minDistance(int dist[], boolean sptSet[]) {
        int min = INF, min_index = -1;
        for (int v = 0; v < V; v++)
            if (!sptSet[v] && dist[v] <= min) {
                min = dist[v];
                min_index = v;
            }
        return min_index;
    }

    static void dijkstra(int graph[][], int src) {
        int dist[] = new int[V];
        boolean sptSet[] = new boolean[V];
        for (int i = 0; i < V; i++) {
            dist[i] = INF;
            sptSet[i] = false;
        }
        dist[src] = 0;
        for (int count = 0; count < V - 1; count++) {
            int u = minDistance(dist, sptSet);
            sptSet[u] = true;
            int[] row = graph[u];
            for (int v = 0; v < V; v++)
                if (!sptSet[v] && row[v] != 0 && dist[u] != INF && dist[u] + row[v] < dist[v])
                    dist[v] = dist[u] + row[v];
        }
    }

    public static void main(String[] args) {
        int graph[][] = new int[V][V];
        for (int i = 0; i < V; i++) {
            for (int j = 0; j < V; j++) {
                if (i == j) graph[i][j] = 0;
                else graph[i][j] = (i * j + 1) % 100;
            }
        }
        long start = System.nanoTime();
        dijkstra(graph, 0);
        System.out.printf("Java Dijkstra: %.6fs\n", (System.nanoTime() - start) / 1e9);
    }
}
