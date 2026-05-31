
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
    cout << "C++ Time: " << time_taken.count() << " s\n";
    return 0;
}
