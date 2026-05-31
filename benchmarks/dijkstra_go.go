package main

import (
	"fmt"
	"time"
)

const V = 1000
const INF = 999999

func minDistance(dist []int, sptSet []bool) int {
	min := INF
	minIndex := -1
	for v := 0; v < V; v++ {
		if !sptSet[v] && dist[v] <= min {
			min = dist[v]
			minIndex = v
		}
	}
	return minIndex
}

func dijkstra(graph [][]int, src int) {
	dist := make([]int, V)
	sptSet := make([]bool, V)
	for i := 0; i < V; i++ {
		dist[i] = INF
		sptSet[i] = false
	}
	dist[src] = 0
	for count := 0; count < V-1; count++ {
		u := minDistance(dist, sptSet)
		sptSet[u] = true
		row := graph[u]
		for v := 0; v < V; v++ {
			if !sptSet[v] && row[v] != 0 && dist[u] != INF && dist[u]+row[v] < dist[v] {
				dist[v] = dist[u] + row[v]
			}
		}
	}
}

func main() {
	graph := make([][]int, V)
	for i := 0; i < V; i++ {
		graph[i] = make([]int, V)
		for j := 0; j < V; j++ {
			if i == j {
				graph[i][j] = 0
			} else {
				graph[i][j] = (i*j + 1) % 100
			}
		}
	}
	start := time.Now()
	dijkstra(graph, 0)
	fmt.Printf("Go Dijkstra: %fs\n", time.Since(start).Seconds())
}
