use std::collections::BinaryHeap;
use std::cmp::Reverse;

fn dijkstra(n: usize, edges: &[(usize, usize, i32)], src: usize) -> Vec<i32> {
    let mut adj = vec![Vec::new(); n];
    for &(u, v, w) in edges {
        adj[u].push((v, w));
        adj[v].push((u, w));
    }
    let mut dist = vec![i32::MAX; n];
    dist[src] = 0;
    let mut pq = BinaryHeap::new();
    pq.push(Reverse((0, src)));
    while let Some(Reverse((d, u))) = pq.pop() {
        if d > dist[u] { continue; }
        for &(v, w) in &adj[u] {
            let nd = d + w;
            if nd < dist[v] {
                dist[v] = nd;
                pq.push(Reverse((nd, v)));
            }
        }
    }
    dist
}

fn main() {
    let n = 1000;
    let mut edges = Vec::new();
    for i in 0..n-1 {
        edges.push((i, i+1, 1));
    }
    let dist = dijkstra(n, &edges, 0);
    println!("{}", dist[n-1]);
}