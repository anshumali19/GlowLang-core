using System;
class Program {
    static int Fib(int n) {
        if (n < 2) return n;
        return Fib(n - 1) + Fib(n - 2);
    }
    static long Loop10M() {
        long s = 0;
        for (int i = 0; i < 10000000; i++) s += i;
        return s;
    }
    static int Ack(int m, int n) {
        if (m == 0) return n + 1;
        if (n == 0) return Ack(m - 1, 1);
        return Ack(m - 1, Ack(m, n - 1));
    }
    static bool IsSafe(int[] board, int row, int col) {
        for (int i = 0; i < row; i++) {
            if (board[i] == col || Math.Abs(board[i] - col) == Math.Abs(i - row))
                return false;
        }
        return true;
    }
    static int nq_count = 0;
    static void Backtrack(int[] board, int row, int n) {
        if (row == n) {
            nq_count++;
            return;
        }
        for (int col = 0; col < n; col++) {
            if (IsSafe(board, row, col)) {
                board[row] = col;
                Backtrack(board, row + 1, n);
                board[row] = -1;
            }
        }
    }
    const int V = 1000;
    const int INF = 999999;
    static int MinDistance(int[] dist, bool[] sptSet) {
        int min = INF, min_index = -1;
        for (int v = 0; v < V; v++) {
            if (!sptSet[v] && dist[v] <= min) {
                min = dist[v];
                min_index = v;
            }
        }
        return min_index;
    }
    static void Dijkstra(int[,] graph, int src) {
        int[] dist = new int[V];
        bool[] sptSet = new bool[V];
        for (int i = 0; i < V; i++) dist[i] = INF;
        dist[src] = 0;
        for (int count = 0; count < V - 1; count++) {
            int u = MinDistance(dist, sptSet);
            sptSet[u] = true;
            for (int v = 0; v < V; v++) {
                if (!sptSet[v] && graph[u, v] != 0 && dist[u] != INF && dist[u] + graph[u, v] < dist[v])
                    dist[v] = dist[u] + graph[u, v];
            }
        }
    }
    static void Main(string[] args) {
        if (args.Length == 0) return;
        string action = args[0];
        if (action == "fib") {
            int total = 0;
            for (int i = 0; i <= 30; i++) total += Fib(i);
            Console.WriteLine(total);
        } else if (action == "loop") {
            Console.WriteLine(Loop10M());
        } else if (action == "ack") {
            Console.WriteLine(Ack(3, 8));
        } else if (action == "nqueens") {
            int n = 11;
            int[] board = new int[n];
            for (int i = 0; i < n; i++) board[i] = -1;
            nq_count = 0;
            Backtrack(board, 0, n);
            Console.WriteLine(nq_count);
        } else if (action == "dijkstra") {
            int[,] graph = new int[V, V];
            for (int i = 0; i < V; i++) {
                for (int j = 0; j < V; j++) {
                    if (i == j) graph[i, j] = 0;
                    else graph[i, j] = (i * j + 1) % 100;
                }
            }
            Dijkstra(graph, 0);
            Console.WriteLine("Done");
        }
    }
}
