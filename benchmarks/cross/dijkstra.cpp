#include <iostream>
#include <vector>

#define V 1000
#define INF 999999

int minDistance(const std::vector<int>& dist, const std::vector<bool>& sptSet) {
    int min = INF, min_index = -1;
    for (int v = 0; v < V; v++) {
        if (!sptSet[v] && dist[v] <= min) {
            min = dist[v];
            min_index = v;
        }
    }
    return min_index;
}

void dijkstra(const std::vector<std::vector<int>>& graph, int src) {
    std::vector<int> dist(V, INF);
    std::vector<bool> sptSet(V, false);

    dist[src] = 0;

    for (int count = 0; count < V - 1; count++) {
        int u = minDistance(dist, sptSet);
        if (u == -1) break;
        sptSet[u] = true;
        for (int v = 0; v < V; v++) {
            if (!sptSet[v] && graph[u][v] != 0 && dist[u] != INF && dist[u] + graph[u][v] < dist[v]) {
                dist[v] = dist[u] + graph[u][v];
            }
        }
    }
}

int main() {
    std::vector<std::vector<int>> graph(V, std::vector<int>(V));
    for (int i = 0; i < V; i++) {
        for (int j = 0; j < V; j++) {
            if (i == j) graph[i][j] = 0;
            else graph[i][j] = (i * j + 1) % 100;
        }
    }

    dijkstra(graph, 0);
    std::cout << "Done" << std::endl;
    return 0;
}
