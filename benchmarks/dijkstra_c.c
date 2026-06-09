#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define V 1000
#define INF 999999

int minDistance(int dist[], int sptSet[]) {
    int min = INF, min_index = -1;
    for (int v = 0; v < V; v++)
        if (sptSet[v] == 0 && dist[v] <= min)
            min = dist[v], min_index = v;
    return min_index;
}

void dijkstra(int **graph, int src) {
    int *dist = malloc(V * sizeof(int));
    int *sptSet = malloc(V * sizeof(int));
    for (int i = 0; i < V; i++)
        dist[i] = INF, sptSet[i] = 0;
    dist[src] = 0;
    for (int count = 0; count < V - 1; count++) {
        int u = minDistance(dist, sptSet);
        sptSet[u] = 1;
        int *row = graph[u];
        for (int v = 0; v < V; v++)
            if (!sptSet[v] && row[v] && dist[u] != INF && dist[u] + row[v] < dist[v])
                dist[v] = dist[u] + row[v];
    }
    free(dist);
    free(sptSet);
}

int main() {
    int **graph = malloc(V * sizeof(int*));
    for (int i = 0; i < V; i++) {
        graph[i] = malloc(V * sizeof(int));
        for (int j = 0; j < V; j++) {
            if (i == j) graph[i][j] = 0;
            else graph[i][j] = (i * j + 1) % 100;
        }
    }
    clock_t start = clock();
    dijkstra(graph, 0);
    printf("C Dijkstra: %fs\n", (double)(clock() - start) / CLOCKS_PER_SEC);
    return 0;
}
