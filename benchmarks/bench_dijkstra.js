const V = 1000;
const INF = 999999;
function minDistance(dist, sptSet) {
    let min = INF, min_index = -1;
    for (let v = 0; v < V; v++) {
        if (!sptSet[v] && dist[v] <= min) {
            min = dist[v];
            min_index = v;
        }
    }
    return min_index;
}
function dijkstra(graph, src) {
    let dist = new Array(V).fill(INF);
    let sptSet = new Array(V).fill(false);
    dist[src] = 0;
    for (let count = 0; count < V - 1; count++) {
        let u = minDistance(dist, sptSet);
        sptSet[u] = true;
        for (let v = 0; v < V; v++) {
            if (!sptSet[v] && graph[u][v] !== 0 && dist[u] !== INF && dist[u] + graph[u][v] < dist[v]) {
                dist[v] = dist[u] + graph[u][v];
            }
        }
    }
}
let graph = new Array(V);
for (let i = 0; i < V; i++) {
    graph[i] = new Array(V);
    for (let j = 0; j < V; j++) {
        if (i === j) graph[i][j] = 0;
        else graph[i][j] = (i * j + 1) % 100;
    }
}
dijkstra(graph, 0);
console.log("Done");
