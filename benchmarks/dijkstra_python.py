import time

V = 1000
INF = 999999

def minDistance(dist, sptSet):
    min_val = INF
    min_index = -1
    for v in range(V):
        if sptSet[v] == 0 and dist[v] <= min_val:
            min_val = dist[v]
            min_index = v
    return min_index

def dijkstra(graph, src):
    dist = [INF] * V
    sptSet = [0] * V
    dist[src] = 0
    for count in range(V - 1):
        u = minDistance(dist, sptSet)
        sptSet[u] = 1
        row = graph[u]
        for v in range(V):
            if not sptSet[v] and row[v] and dist[u] != INF and dist[u] + row[v] < dist[v]:
                dist[v] = dist[u] + row[v]

graph = [[0] * V for i in range(V)]
for i in range(V):
    for j in range(V):
        if i == j: graph[i][j] = 0
        else: graph[i][j] = (i * j + 1) % 100

start = time.time()
dijkstra(graph, 0)
print(f"Python Dijkstra: {time.time() - start:.6f}s")
