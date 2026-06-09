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
        if u == -1:
            break
        sptSet[u] = True
        for v in range(V):
            if not sptSet[v] and graph[u][v] != 0 and dist[u] != INF and dist[u] + graph[u][v] < dist[v]:
                dist[v] = dist[u] + graph[u][v]

graph = [[0] * V for _ in range(V)]
for i in range(V):
    for j in range(V):
        if i == j:
            graph[i][j] = 0
        else:
            graph[i][j] = (i * j + 1) % 100

dijkstra(graph, 0)
print("Done")
