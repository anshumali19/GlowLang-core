
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
    printf("C Time: %f s\n", time_taken);
    free(graph);
    return 0;
}
