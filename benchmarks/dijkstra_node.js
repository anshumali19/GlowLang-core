const { performance } = require('perf_hooks');

const V = 1000;
const INF = 999999;

function minDistance(dist, sptSet) {
    let min = INF, min_index = -1;
    for (let v = 0; v < V; v++)
        if (sptSet[v] === 0 && dist[v] <= min)
            min = dist[v], min_index = v;
    return min_index;
}

function dijkstra(graph, src) {
    let dist = new Int32Array(V);
    let sptSet = new Uint8Array(V);
    for (let i = 0; i < V; i++)
        dist[i] = INF, sptSet[i] = 0;
    dist[src] = 0;
    for (let count = 0; count < V - 1; count++) {
        let u = minDistance(dist, sptSet);
        sptSet[u] = 1;
        let row = graph[u];
        for (let v = 0; v < V; v++)
            if (!sptSet[v] && row[v] && dist[u] !== INF && dist[u] + row[v] < dist[v])
                dist[v] = dist[u] + row[v];
    }
}

let graph = new Array(V);
for (let i = 0; i < V; i++) {
    graph[i] = new Int32Array(V);
    for (let j = 0; j < V; j++) {
        if (i === j) graph[i][j] = 0;
        else graph[i][j] = (i * j + 1) % 100;
    }
}

let start = performance.now();
dijkstra(graph, 0);
console.log(`Node Dijkstra: ${(performance.now() - start) / 1000}s`);
