import subprocess
import time
import os
import sys

# Standardized parameters
V = 1000
N_QUEENS = 11
FIB_MAX = 30
LOOP_LIMIT = 10000000

print("Generating benchmark files...")

# Ensure benchmarks dir exists
os.makedirs("benchmarks", exist_ok=True)

# 1. C Files
with open("benchmarks/bench_fib.c", "w") as f:
    f.write("""#include <stdio.h>
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main() {
    int total = 0;
    for (int i = 0; i <= 30; i++) total += fib(i);
    printf("%d\\n", total);
    return 0;
}
""")

with open("benchmarks/bench_loop.c", "w") as f:
    f.write("""#include <stdio.h>
int main() {
    long long total = 0;
    for (int i = 0; i < 10000000; i++) total += i;
    printf("%lld\\n", total);
    return 0;
}
""")

with open("benchmarks/dsa_ackermann.c", "w") as f:
    f.write("""#include <stdio.h>
int ack(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}
int main() {
    printf("%d\\n", ack(3, 8));
    return 0;
}
""")

with open("benchmarks/dsa_nqueens.c", "w") as f:
    f.write("""#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int is_safe(int *board, int row, int col) {
    for (int i = 0; i < row; i++) {
        if (board[i] == col || abs(board[i] - col) == abs(i - row))
            return 0;
    }
    return 1;
}
int nq_count = 0;
void backtrack(int *board, int row, int n) {
    if (row == n) {
        nq_count++;
        return;
    }
    for (int col = 0; col < n; col++) {
        if (is_safe(board, row, col)) {
            board[row] = col;
            backtrack(board, row + 1, n);
            board[row] = -1;
        }
    }
}
int main() {
    int n = 11;
    int *board = (int *)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) board[i] = -1;
    backtrack(board, 0, n);
    printf("%d\\n", nq_count);
    free(board);
    return 0;
}
""")

with open("benchmarks/bench_dijkstra.c", "w") as f:
    f.write("""#include <stdio.h>
#include <stdlib.h>
#define V 1000
#define INF 999999
int minDistance(int *dist, int *sptSet) {
    int min = INF, min_index = -1;
    for (int v = 0; v < V; v++) {
        if (sptSet[v] == 0 && dist[v] <= min) {
            min = dist[v];
            min_index = v;
        }
    }
    return min_index;
}
void dijkstra(int graph[V][V], int src) {
    int dist[V];
    int sptSet[V];
    for (int i = 0; i < V; i++) {
        dist[i] = INF;
        sptSet[i] = 0;
    }
    dist[src] = 0;
    for (int count = 0; count < V - 1; count++) {
        int u = minDistance(dist, sptSet);
        sptSet[u] = 1;
        for (int v = 0; v < V; v++) {
            if (sptSet[v] == 0 && graph[u][v] != 0 && dist[u] != INF && dist[u] + graph[u][v] < dist[v]) {
                dist[v] = dist[u] + graph[u][v];
            }
        }
    }
}
int graph[V][V];
int main() {
    for (int i = 0; i < V; i++) {
        for (int j = 0; j < V; j++) {
            if (i == j) graph[i][j] = 0;
            else graph[i][j] = (i * j + 1) % 100;
        }
    }
    dijkstra(graph, 0);
    printf("Done\\n");
    return 0;
}
""")

# 2. C++ Files (just copy C logic with g++ compatibility)
for name in ["bench_fib", "bench_loop", "dsa_ackermann", "dsa_nqueens", "bench_dijkstra"]:
    with open(f"benchmarks/{name}.c", "r") as src, open(f"benchmarks/{name}.cpp", "w") as dst:
        dst.write(src.read())

# 3. Go Files
with open("benchmarks/bench_fib.go", "w") as f:
    f.write("""package main
import "fmt"
func fib(n int) int {
    if n < 2 { return n }
    return fib(n-1) + fib(n-2)
}
func main() {
    total := 0
    for i := 0; i <= 30; i++ { total += fib(i) }
    fmt.Println(total)
}
""")

with open("benchmarks/bench_loop.go", "w") as f:
    f.write("""package main
import "fmt"
func main() {
    var total int64 = 0
    for i := 0; i < 10000000; i++ { total += int64(i) }
    fmt.Println(total)
}
""")

with open("benchmarks/dsa_ackermann.go", "w") as f:
    f.write("""package main
import "fmt"
func ack(m, n int) int {
    if m == 0 { return n + 1 }
    if n == 0 { return ack(m-1, 1) }
    return ack(m-1, ack(m, n-1))
}
func main() {
    fmt.Println(ack(3, 8))
}
""")

with open("benchmarks/dsa_nqueens.go", "w") as f:
    f.write("""package main
import "fmt"
func is_safe(board []int, row, col int) bool {
    for i := 0; i < row; i++ {
        diff := col - board[i]
        if diff < 0 { diff = -diff }
        if board[i] == col || diff == (row - i) {
            return false
        }
    }
    return true
}
var nq_count = 0
func backtrack(board []int, row, n int) {
    if row == n {
        nq_count++
        return
    }
    for col := 0; col < n; col++ {
        if is_safe(board, row, col) {
            board[row] = col
            backtrack(board, row+1, n)
            board[row] = -1
        }
    }
}
func main() {
    n := 11
    board := make([]int, n)
    for i := range board { board[i] = -1 }
    backtrack(board, 0, n)
    fmt.Println(nq_count)
}
""")

with open("benchmarks/bench_dijkstra.go", "w") as f:
    f.write("""package main
import "fmt"
const V = 1000
const INF = 999999
func minDistance(dist []int, sptSet []bool) int {
    min := INF
    min_index := -1
    for v := 0; v < V; v++ {
        if !sptSet[v] && dist[v] <= min {
            min = dist[v]
            min_index = v
        }
    }
    return min_index
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
        for v := 0; v < V; v++ {
            if !sptSet[v] && graph[u][v] != 0 && dist[u] != INF && dist[u]+graph[u][v] < dist[v] {
                dist[v] = dist[u] + graph[u][v]
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
    dijkstra(graph, 0)
    fmt.Println("Done")
}
""")

# 4. Java Files
with open("benchmarks/BenchFib.java", "w") as f:
    f.write("""public class BenchFib {
    static int fib(int n) {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
    }
    public static void main(String[] args) {
        int total = 0;
        for (int i = 0; i <= 30; i++) total += fib(i);
        System.out.println(total);
    }
}
""")

with open("benchmarks/BenchLoop.java", "w") as f:
    f.write("""public class BenchLoop {
    public static void main(String[] args) {
        long total = 0;
        for (int i = 0; i < 10000000; i++) total += i;
        System.out.println(total);
    }
}
""")

with open("benchmarks/DsaAckermann.java", "w") as f:
    f.write("""public class DsaAckermann {
    static int ack(int m, int n) {
        if (m == 0) return n + 1;
        if (n == 0) return ack(m - 1, 1);
        return ack(m - 1, ack(m, n - 1));
    }
    public static void main(String[] args) {
        System.out.println(ack(3, 8));
    }
}
""")

with open("benchmarks/DsaNQueens.java", "w") as f:
    f.write("""public class DsaNQueens {
    static boolean isSafe(int[] board, int row, int col) {
        for (int i = 0; i < row; i++) {
            if (board[i] == col || Math.abs(board[i] - col) == Math.abs(i - row))
                return false;
        }
        return true;
    }
    static int nqCount = 0;
    static void backtrack(int[] board, int row, int n) {
        if (row == n) {
            nqCount++;
            return;
        }
        for (int col = 0; col < n; col++) {
            if (isSafe(board, row, col)) {
                board[row] = col;
                backtrack(board, row + 1, n);
                board[row] = -1;
            }
        }
    }
    public static void main(String[] args) {
        int n = 11;
        int[] board = new int[n];
        for (int i = 0; i < n; i++) board[i] = -1;
        backtrack(board, 0, n);
        System.out.println(nqCount);
    }
}
""")

with open("benchmarks/DijkstraBench.java", "w") as f:
    f.write("""public class DijkstraBench {
    static final int V = 1000;
    static final int INF = 999999;
    static int minDistance(int[] dist, boolean[] sptSet) {
        int min = INF, min_index = -1;
        for (int v = 0; v < V; v++) {
            if (!sptSet[v] && dist[v] <= min) {
                min = dist[v];
                min_index = v;
            }
        }
        return min_index;
    }
    static void dijkstra(int[][] graph, int src) {
        int[] dist = new int[V];
        boolean[] sptSet = new boolean[V];
        for (int i = 0; i < V; i++) {
            dist[i] = INF;
            sptSet[i] = false;
        }
        dist[src] = 0;
        for (int count = 0; count < V - 1; count++) {
            int u = minDistance(dist, sptSet);
            sptSet[u] = true;
            for (int v = 0; v < V; v++) {
                if (!sptSet[v] && graph[u][v] != 0 && dist[u] != INF && dist[u] + graph[u][v] < dist[v]) {
                    dist[v] = dist[u] + graph[u][v];
                }
            }
        }
    }
    public static void main(String[] args) {
        int[][] graph = new int[V][V];
        for (int i = 0; i < V; i++) {
            for (int j = 0; j < V; j++) {
                if (i == j) graph[i][j] = 0;
                else graph[i][j] = (i * j + 1) % 100;
            }
        }
        dijkstra(graph, 0);
        System.out.println("Done");
    }
}
""")

# 5. C# (Updated CSharpBench/Program.cs)
with open("benchmarks/CSharpBench/Program.cs", "w") as f:
    f.write("""using System;
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
""")

# 6. Python Files
with open("benchmarks/bench_fib.py", "w") as f:
    f.write("""def fib(n):
    if n < 2: return n
    return fib(n - 1) + fib(n - 2)
total = 0
for i in range(31): total += fib(i)
print(total)
""")

with open("benchmarks/bench_loop.py", "w") as f:
    f.write("""total = 0
for i in range(10000000): total += i
print(total)
""")

with open("benchmarks/dsa_ackermann.py", "w") as f:
    f.write("""import sys
sys.setrecursionlimit(50000)
def ack(m, n):
    if m == 0: return n + 1
    if n == 0: return ack(m - 1, 1)
    return ack(m - 1, ack(m, n - 1))
print(ack(3, 8))
""")

with open("benchmarks/dsa_nqueens.py", "w") as f:
    f.write("""def is_safe(board, row, col):
    for i in range(row):
        if board[i] == col or abs(board[i] - col) == abs(i - row):
            return False
    return True
nq_count = 0
def backtrack(board, row, n):
    global nq_count
    if row == n:
        nq_count += 1
        return
    for col in range(n):
        if is_safe(board, row, col):
            board[row] = col
            backtrack(board, row + 1, n)
            board[row] = -1
n = 11
board = [-1] * n
backtrack(board, 0, n)
print(nq_count)
""")

with open("benchmarks/bench_dijkstra.py", "w") as f:
    f.write("""V = 1000
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
        sptSet[u] = True
        for v in range(V):
            if not sptSet[v] and graph[u][v] != 0 and dist[u] != INF and dist[u] + graph[u][v] < dist[v]:
                dist[v] = dist[u] + graph[u][v]
graph = [[0]*V for _ in range(V)]
for i in range(V):
    for j in range(V):
        if i == j: graph[i][j] = 0
        else: graph[i][j] = (i * j + 1) % 100
dijkstra(graph, 0)
print("Done")
""")

# 7. GlowLang Files
with open("benchmarks/bench_fib.glow", "w") as f:
    f.write("""blueprint Fibonacci:
    action calculate(n):
        when n <= 1:
            return n
        otherwise:
            return self.calculate(n - 1) + self.calculate(n - 2)
        end
    end
end
let calculator = Fibonacci()
let total = 0
let i = 0
while i <= 30:
    total = total + calculator.calculate(i)
    i = i + 1
end
show total
""")

with open("benchmarks/bench_loop.glow", "w") as f:
    f.write("""let total = 0
let i = 0
while i < 10000000:
    total = total + i
    i = i + 1
end
show total
""")

with open("benchmarks/dsa_ackermann.glow", "w") as f:
    f.write("""blueprint Ackermann:
    action calculate(m, n):
        when m is 0:
            return n + 1
        end
        when n is 0:
            return self.calculate(m - 1, 1)
        end
        return self.calculate(m - 1, self.calculate(m, n - 1))
    end
end
let ackObj = Ackermann()
show ackObj.calculate(3, 8)
""")

with open("benchmarks/dsa_nqueens.glow", "w") as f:
    f.write("""blueprint NQueens:
    action init(n):
        self.n = n
        self.count = 0
    end
    action is_safe(board, row, col):
        let i = 0
        while i < row:
            let b = board[i]
            if b is col:
                return false
            end
            let diff = col - b
            if diff < 0:
                diff = 0 - diff
            end
            if diff is (row - i):
                return false
            end
            i = i + 1
        end
        return true
    end
    action backtrack(board, row):
        when row is self.n:
            self.count = self.count + 1
            return 0
        end
        let col = 0
        while col < self.n:
            when self.is_safe(board, row, col):
                board[row] = col
                self.backtrack(board, row + 1)
                board[row] = 0 - 1
            end
            col = col + 1
        end
    end
end
let n = 11
let nq = NQueens()
nq.init(n)
let board = range(n)
let i = 0
while i < n:
    board[i] = 0 - 1
    i = i + 1
end
nq.backtrack(board, 0)
show nq.count
""")

with open("benchmarks/bench_dijkstra.glow", "w") as f:
    f.write("""blueprint Dijkstra:
    action init(v):
        self.V = v
        self.INF = 999999
    end
    action minDistance(dist, sptSet):
        let min_val = self.INF
        let min_index = 0 - 1
        let v = 0
        while v < self.V:
            when sptSet[v] is 0:
                when dist[v] <= min_val:
                    min_val = dist[v]
                    min_index = v
                end
            end
            v = v + 1
        end
        return min_index
    end
    action solve(graph, src):
        let dist = range(self.V)
        let sptSet = range(self.V)
        let i = 0
        while i < self.V:
            dist[i] = self.INF
            sptSet[i] = 0
            i = i + 1
        end
        dist[src] = 0
        
        let count = 0
        while count < (self.V - 1):
            let u = self.minDistance(dist, sptSet)
            sptSet[u] = 1
            let v = 0
            let row = graph[u]
            while v < self.V:
                when sptSet[v] is 0:
                    when row[v] != 0:
                        when dist[u] != self.INF:
                            when (dist[u] + row[v]) < dist[v]:
                                dist[v] = dist[u] + row[v]
                            end
                        end
                    end
                end
                v = v + 1
            end
            count = count + 1
        end
    end
end
let V = 1000
let graph = range(V)
let i = 0
while i < V:
    let row = range(V)
    let j = 0
    while j < V:
        when i is j:
            row[j] = 0
        end
        when i != j:
            row[j] = (i * j + 1) % 100
        end
        j = j + 1
    end
    graph[i] = row
    i = i + 1
end
let d = Dijkstra()
d.init(V)
d.solve(graph, 0)
show "Done"
""")

# 8. JS Files (Node and Bun)
with open("benchmarks/bench_fib.js", "w") as f:
    f.write("""function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
let total = 0;
for (let i = 0; i <= 30; i++) total += fib(i);
console.log(total);
""")

with open("benchmarks/bench_loop.js", "w") as f:
    f.write("""let total = 0;
for (let i = 0; i < 10000000; i++) total += i;
console.log(total);
""")

with open("benchmarks/dsa_ackermann.js", "w") as f:
    f.write("""function ack(m, n) {
    if (m === 0) return n + 1;
    if (n === 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}
console.log(ack(3, 8));
""")

with open("benchmarks/dsa_nqueens.js", "w") as f:
    f.write("""function isSafe(board, row, col) {
    for (let i = 0; i < row; i++) {
        if (board[i] === col || Math.abs(board[i] - col) === Math.abs(i - row))
            return false;
    }
    return true;
}
let nq_count = 0;
function backtrack(board, row, n) {
    if (row === n) {
        nq_count++;
        return;
    }
    for (let col = 0; col < n; col++) {
        if (isSafe(board, row, col)) {
            board[row] = col;
            backtrack(board, row + 1, n);
            board[row] = -1;
        }
    }
}
const n = 11;
let board = new Array(n).fill(-1);
backtrack(board, 0, n);
console.log(nq_count);
""")

with open("benchmarks/bench_dijkstra.js", "w") as f:
    f.write("""const V = 1000;
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
""")

# 9. Perl Files
with open("benchmarks/bench_fib.pl", "w") as f:
    f.write("""sub fib {
    my ($n) = @_;
    return $n if $n < 2;
    return fib($n - 1) + fib($n - 2);
}
my $total = 0;
for (my $i = 0; $i <= 30; $i++) { $total += fib($i); }
print "$total\\n";
""")

with open("benchmarks/bench_loop.pl", "w") as f:
    f.write("""my $total = 0;
for (my $i = 0; $i < 10000000; $i++) { $total += $i; }
print "$total\\n";
""")

with open("benchmarks/dsa_ackermann.pl", "w") as f:
    f.write("""no warnings 'recursion';
sub ack {
    my ($m, $n) = @_;
    return $n + 1 if $m == 0;
    return ack($m - 1, 1) if $n == 0;
    return ack($m - 1, ack($m, $n - 1));
}
print ack(3, 8) . "\\n";
""")

with open("benchmarks/dsa_nqueens.pl", "w") as f:
    f.write("""my $nq_count = 0;
sub is_safe {
    my ($board, $row, $col) = @_;
    for (my $i = 0; $i < $row; $i++) {
        my $b = $board->[$i];
        if ($b == $col || abs($b - $col) == abs($i - $row)) {
            return 0;
        }
    }
    return 1;
}
sub backtrack {
    my ($board, $row, $n) = @_;
    if ($row == $n) {
        $nq_count++;
        return;
    }
    for (my $col = 0; $col < $n; $col++) {
        if (is_safe($board, $row, $col)) {
            $board->[$row] = $col;
            backtrack($board, $row + 1, $n);
            $board->[$row] = -1;
        }
    }
}
my $n = 11;
my @board = (-1) x $n;
backtrack(\\@board, 0, $n);
print "$nq_count\\n";
""")

with open("benchmarks/bench_dijkstra.pl", "w") as f:
    f.write("""my $V = 1000;
my $INF = 999999;
sub minDistance {
    my ($dist, $sptSet) = @_;
    my $min_val = $INF;
    my $min_index = -1;
    for (my $v = 0; $v < $V; $v++) {
        if (!$sptSet->[$v] && $dist->[$v] <= $min_val) {
            $min_val = $dist->[$v];
            $min_index = $v;
        }
    }
    return $min_index;
}
sub dijkstra {
    my ($graph, $src) = @_;
    my @dist = ($INF) x $V;
    my @sptSet = (0) x $V;
    $dist[$src] = 0;
    for (my $count = 0; $count < $V - 1; $count++) {
        my $u = minDistance(\\@dist, \\@sptSet);
        $sptSet[$u] = 1;
        my $row = $graph->[$u];
        for (my $v = 0; $v < $V; $v++) {
            if (!$sptSet[$v] && $row->[$v] != 0 && $dist[$u] != $INF && $dist[$u] + $row->[$v] < $dist[$v]) {
                $dist[$v] = $dist[$u] + $row->[$v];
            }
        }
    }
}
my @graph;
for (my $i = 0; $i < $V; $i++) {
    my @row;
    for (my $j = 0; $j < $V; $j++) {
        if ($i == $j) {
            $row[$j] = 0;
        } else {
            $row[$j] = ($i * $j + 1) % 100;
        }
    }
    $graph[$i] = \\@row;
}
dijkstra(\\@graph, 0);
print "Done\\n";
""")

print("Compiling compiled languages...")

def run_cmd(cmd):
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Failed cmd: {cmd}\\nError: {res.stderr}")
        return False
    return True

run_cmd("gcc -O3 benchmarks/bench_fib.c -o benchmarks/bench_fib_c")
run_cmd("gcc -O3 benchmarks/bench_loop.c -o benchmarks/bench_loop_c")
run_cmd("gcc -O3 benchmarks/dsa_ackermann.c -o benchmarks/ack_c -lm")
run_cmd("gcc -O3 benchmarks/dsa_nqueens.c -o benchmarks/nq_c")
run_cmd("gcc -O3 benchmarks/bench_dijkstra.c -o benchmarks/bench_dijkstra_c")

run_cmd("g++ -O3 benchmarks/bench_fib.cpp -o benchmarks/bench_fib_cpp")
run_cmd("g++ -O3 benchmarks/bench_loop.cpp -o benchmarks/bench_loop_cpp")
run_cmd("g++ -O3 benchmarks/dsa_ackermann.cpp -o benchmarks/ack_cpp")
run_cmd("g++ -O3 benchmarks/dsa_nqueens.cpp -o benchmarks/nq_cpp")
run_cmd("g++ -O3 benchmarks/bench_dijkstra.cpp -o benchmarks/bench_dijkstra_cpp")

run_cmd("go build -o benchmarks/bench_fib_go benchmarks/bench_fib.go")
run_cmd("go build -o benchmarks/bench_loop_go benchmarks/bench_loop.go")
run_cmd("go build -o benchmarks/ack_go benchmarks/dsa_ackermann.go")
run_cmd("go build -o benchmarks/nq_go benchmarks/dsa_nqueens.go")
run_cmd("go build -o benchmarks/bench_dijkstra_go benchmarks/bench_dijkstra.go")

run_cmd("javac benchmarks/BenchFib.java benchmarks/BenchLoop.java benchmarks/DsaAckermann.java benchmarks/DsaNQueens.java benchmarks/DijkstraBench.java")

run_cmd("dotnet build benchmarks/CSharpBench/CSharpBench.csproj -c Release")

# Benchmark definitions
languages = ["GlowLang", "C", "C++", "Go", "Java", "C#", "Python", "Node.js", "Bun", "Perl"]
benchmarks = ["Fibonacci", "Loop", "Ackermann", "N-Queens", "Dijkstra"]

commands = {
    "Fibonacci": {
        "GlowLang": "./bin/glowlang benchmarks/bench_fib.glow",
        "C": "./benchmarks/bench_fib_c",
        "C++": "./benchmarks/bench_fib_cpp",
        "Go": "./benchmarks/bench_fib_go",
        "Java": "java -cp benchmarks BenchFib",
        "C#": "dotnet benchmarks/CSharpBench/bin/Release/net8.0/CSharpBench.dll fib",
        "Python": "python3 benchmarks/bench_fib.py",
        "Node.js": "node benchmarks/bench_fib.js",
        "Bun": "/home/anshumali/.bun/bin/bun benchmarks/bench_fib.js",
        "Perl": "perl benchmarks/bench_fib.pl"
    },
    "Loop": {
        "GlowLang": "./bin/glowlang benchmarks/bench_loop.glow",
        "C": "./benchmarks/bench_loop_c",
        "C++": "./benchmarks/bench_loop_cpp",
        "Go": "./benchmarks/bench_loop_go",
        "Java": "java -cp benchmarks BenchLoop",
        "C#": "dotnet benchmarks/CSharpBench/bin/Release/net8.0/CSharpBench.dll loop",
        "Python": "python3 benchmarks/bench_loop.py",
        "Node.js": "node benchmarks/bench_loop.js",
        "Bun": "/home/anshumali/.bun/bin/bun benchmarks/bench_loop.js",
        "Perl": "perl benchmarks/bench_loop.pl"
    },
    "Ackermann": {
        "GlowLang": "./bin/glowlang benchmarks/dsa_ackermann.glow",
        "C": "./benchmarks/ack_c",
        "C++": "./benchmarks/ack_cpp",
        "Go": "./benchmarks/ack_go",
        "Java": "java -cp benchmarks DsaAckermann",
        "C#": "dotnet benchmarks/CSharpBench/bin/Release/net8.0/CSharpBench.dll ack",
        "Python": "python3 benchmarks/dsa_ackermann.py",
        "Node.js": "node benchmarks/dsa_ackermann.js",
        "Bun": "/home/anshumali/.bun/bin/bun benchmarks/dsa_ackermann.js",
        "Perl": "perl benchmarks/dsa_ackermann.pl"
    },
    "N-Queens": {
        "GlowLang": "./bin/glowlang benchmarks/dsa_nqueens.glow",
        "C": "./benchmarks/nq_c",
        "C++": "./benchmarks/nq_cpp",
        "Go": "./benchmarks/nq_go",
        "Java": "java -cp benchmarks DsaNQueens",
        "C#": "dotnet benchmarks/CSharpBench/bin/Release/net8.0/CSharpBench.dll nqueens",
        "Python": "python3 benchmarks/dsa_nqueens.py",
        "Node.js": "node benchmarks/dsa_nqueens.js",
        "Bun": "/home/anshumali/.bun/bin/bun benchmarks/dsa_nqueens.js",
        "Perl": "perl benchmarks/dsa_nqueens.pl"
    },
    "Dijkstra": {
        "GlowLang": "./bin/glowlang benchmarks/bench_dijkstra.glow",
        "C": "./benchmarks/bench_dijkstra_c",
        "C++": "./benchmarks/bench_dijkstra_cpp",
        "Go": "./benchmarks/bench_dijkstra_go",
        "Java": "java -cp benchmarks DijkstraBench",
        "C#": "dotnet benchmarks/CSharpBench/bin/Release/net8.0/CSharpBench.dll dijkstra",
        "Python": "python3 benchmarks/bench_dijkstra.py",
        "Node.js": "node benchmarks/bench_dijkstra.js",
        "Bun": "/home/anshumali/.bun/bin/bun benchmarks/bench_dijkstra.js",
        "Perl": "perl benchmarks/bench_dijkstra.pl"
    }
}

def run_bench(cmd):
    start = time.time()
    try:
        subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
    except Exception as e:
        return None
    return time.time() - start

results = {b: {} for b in benchmarks}

print("Running benchmarks...")
for b in benchmarks:
    print(f"Benchmarking {b}...")
    for lang in languages:
        cmd = commands[b][lang]
        # Warmup
        run_bench(cmd)
        # Average of 3 runs
        times = []
        for _ in range(3):
            t = run_bench(cmd)
            if t is not None:
                times.append(t)
        if times:
            results[b][lang] = sum(times) / len(times)
        else:
            results[b][lang] = "FAIL"

# Print Table
header = "| Benchmark | " + " | ".join(languages) + " |"
separator = "| :--- | " + " | ".join([":---:"] * len(languages)) + " |"
rows = []
for b in benchmarks:
    row = f"| **{b}** | "
    for lang in languages:
        val = results[b].get(lang, "N/A")
        if isinstance(val, float):
            row += f"{val:.4f}s | "
        else:
            row += f"{val} | "
    rows.append(row)

markdown_table = "\n".join([header, separator] + rows)
print("\nResults:")
print(markdown_table)

with open("benchmarks/bench_results_cross.md", "w") as f:
    f.write("# Comprehensive Standardized Cross-Language Benchmarks\n\n")
    f.write(markdown_table)
    f.write("\n\n*Measurements include runtime startup and execution (average of 3 runs after warmup).*")

print("Done. Saved results to benchmarks/bench_results_cross.md")
