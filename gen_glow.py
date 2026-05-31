import os

glow_files = {
    "dsa_bst.glow": """/* Binary Search Tree */
blueprint Node:
    can init(v):
        self.v = v
        self.left = 0
        self.right = 0
    end
end

blueprint BST:
    can init():
        self.root = 0
    end
    can insert(node, v):
        if node == 0:
            let n = Node()
            n.init(v)
            return n
        end
        if v < node.v:
            node.left = self.insert(node.left, v)
        end
        if v > node.v:
            node.right = self.insert(node.right, v)
        end
        return node
    end
    can search(node, v):
        if node == 0:
            return false
        end
        if node.v == v:
            return true
        end
        if v < node.v:
            return self.search(node.left, v)
        end
        return self.search(node.right, v)
    end
end
let tree = BST()
tree.init()
tree.root = tree.insert(tree.root, 10)
tree.root = tree.insert(tree.root, 5)
tree.root = tree.insert(tree.root, 15)
show tree.search(tree.root, 5)
show tree.search(tree.root, 20)
""",

    "dsa_knapsack.glow": """/* Knapsack Problem DP */
func max(a, b):
    if a > b: return a end
    return b
end

func knapsack(W, wt, val, n):
    let dp = range(W + 1)
    let i = 0
    while i <= W:
        dp[i] = 0
        i = i + 1
    end
    
    let j = 0
    while j < n:
        let w = W
        while w >= wt[j]:
            dp[w] = max(dp[w], dp[w - wt[j]] + val[j])
            w = w - 1
        end
        j = j + 1
    end
    return dp[W]
end

let val = [60, 100, 120]
let wt = [10, 20, 30]
show knapsack(50, wt, val, 3)
""",

    "dsa_lcs.glow": """/* Longest Common Subsequence DP */
func max(a, b):
    if a > b: return a end
    return b
end

func lcs(X, Y, m, n):
    let L = range(m + 1)
    let i = 0
    while i <= m:
        let row = range(n + 1)
        let j = 0
        while j <= n: row[j] = 0 j = j + 1 end
        L[i] = row
        i = i + 1
    end

    i = 1
    while i <= m:
        let j = 1
        while j <= n:
            if X[i - 1] == Y[j - 1]:
                let prev_row = L[i - 1]
                let curr_row = L[i]
                curr_row[j] = prev_row[j - 1] + 1
            end
            if X[i - 1] != Y[j - 1]:
                let prev_row = L[i - 1]
                let curr_row = L[i]
                curr_row[j] = max(prev_row[j], curr_row[j - 1])
            end
            j = j + 1
        end
        i = i + 1
    end
    let final_row = L[m]
    return final_row[n]
end

let X = [1, 2, 3, 4, 1]
let Y = [3, 4, 1, 2, 1, 3]
show lcs(X, Y, 5, 6)
""",

    "dsa_lis.glow": """/* Longest Increasing Subsequence */
func max(a, b):
    if a > b: return a end
    return b
end

func lis(arr, n):
    let dp = range(n)
    let i = 0
    while i < n: dp[i] = 1 i = i + 1 end
    
    i = 1
    while i < n:
        let j = 0
        while j < i:
            if arr[i] > arr[j]:
                dp[i] = max(dp[i], dp[j] + 1)
            end
            j = j + 1
        end
        i = i + 1
    end
    
    let ans = 0
    i = 0
    while i < n:
        if dp[i] > ans: ans = dp[i] end
        i = i + 1
    end
    return ans
end

let arr = [10, 22, 9, 33, 21, 50, 41, 60]
show lis(arr, 8)
""",

    "dsa_dijkstra.glow": """/* Dijkstra Shortest Path */
func dijkstra(graph, V, src):
    let dist = range(V)
    let sptSet = range(V)
    let i = 0
    while i < V:
        dist[i] = 999999
        sptSet[i] = 0
        i = i + 1
    end
    dist[src] = 0
    
    let count = 0
    while count < V - 1:
        let u = minDistance(dist, sptSet, V)
        sptSet[u] = 1
        
        let v = 0
        let row = graph[u]
        while v < V:
            if sptSet[v] == 0:
                if row[v] != 0:
                    if dist[u] != 999999:
                        if dist[u] + row[v] < dist[v]:
                            dist[v] = dist[u] + row[v]
                        end
                    end
                end
            end
            v = v + 1
        end
        count = count + 1
    end
    return dist
end

func minDistance(dist, sptSet, V):
    let min_val = 999999
    let min_index = 0 - 1
    let v = 0
    while v < V:
        if sptSet[v] == 0:
            if dist[v] <= min_val:
                min_val = dist[v]
                min_index = v
            end
        end
        v = v + 1
    end
    return min_index
end

let graph = [
    [0, 4, 0, 0, 0, 0, 0, 8, 0],
    [4, 0, 8, 0, 0, 0, 0, 11, 0],
    [0, 8, 0, 7, 0, 4, 0, 0, 2],
    [0, 0, 7, 0, 9, 14, 0, 0, 0],
    [0, 0, 0, 9, 0, 10, 0, 0, 0],
    [0, 0, 4, 14, 10, 0, 2, 0, 0],
    [0, 0, 0, 0, 0, 2, 0, 1, 6],
    [8, 11, 0, 0, 0, 0, 1, 0, 7],
    [0, 0, 2, 0, 0, 0, 6, 7, 0]
]
show dijkstra(graph, 9, 0)
""",

    "dsa_bellmanford.glow": """/* Bellman-Ford Algorithm */
func bellmanFord(edges, V, E, src):
    let dist = range(V)
    let i = 0
    while i < V:
        dist[i] = 999999
        i = i + 1
    end
    dist[src] = 0
    
    i = 1
    while i < V:
        let j = 0
        while j < E:
            let edge = edges[j]
            let u = edge[0]
            let v = edge[1]
            let weight = edge[2]
            if dist[u] != 999999:
                if dist[u] + weight < dist[v]:
                    dist[v] = dist[u] + weight
                end
            end
            j = j + 1
        end
        i = i + 1
    end
    return dist
end

let edges = [
    [0, 1, -1],
    [0, 2, 4],
    [1, 2, 3],
    [1, 3, 2],
    [1, 4, 2],
    [3, 2, 5],
    [3, 1, 1],
    [4, 3, -3]
]
show bellmanFord(edges, 5, 8, 0)
""",

    "dsa_floydwarshall.glow": """/* Floyd-Warshall Algorithm */
func floydWarshall(graph, V):
    let dist = range(V)
    let i = 0
    while i < V:
        let row = range(V)
        let orig_row = graph[i]
        let j = 0
        while j < V:
            row[j] = orig_row[j]
            j = j + 1
        end
        dist[i] = row
        i = i + 1
    end
    
    let k = 0
    while k < V:
        i = 0
        while i < V:
            let j = 0
            while j < V:
                let dist_i = dist[i]
                let dist_k = dist[k]
                if dist_i[k] + dist_k[j] < dist_i[j]:
                    dist_i[j] = dist_i[k] + dist_k[j]
                end
                j = j + 1
            end
            i = i + 1
        end
        k = k + 1
    end
    return dist
end

let INF = 999999
let graph = [
    [0, 5, INF, 10],
    [INF, 0, 3, INF],
    [INF, INF, 0, 1],
    [INF, INF, INF, 0]
]
show floydWarshall(graph, 4)
""",

    "dsa_trie.glow": """/* Trie Prefix Tree */
blueprint TrieNode:
    can init():
        self.children = range(26)
        let i = 0
        while i < 26:
            self.children[i] = 0
            i = i + 1
        end
        self.isEndOfWord = false
    end
end

blueprint Trie:
    can init():
        self.root = TrieNode()
        self.root.init()
    end
    can insert(word, length):
        let pCrawl = self.root
        let i = 0
        while i < length:
            let index = word[i] - 97
            let children = pCrawl.children
            if children[index] == 0:
                let newNode = TrieNode()
                newNode.init()
                children[index] = newNode
            end
            pCrawl = children[index]
            i = i + 1
        end
        pCrawl.isEndOfWord = true
    end
    can search(word, length):
        let pCrawl = self.root
        let i = 0
        while i < length:
            let index = word[i] - 97
            let children = pCrawl.children
            if children[index] == 0:
                return false
            end
            pCrawl = children[index]
            i = i + 1
        end
        return pCrawl.isEndOfWord
    end
end

let t = Trie()
t.init()
let word1 = [97, 112, 112, 108, 101] /* apple */
t.insert(word1, 5)
show t.search(word1, 5)
""",

    "dsa_segmenttree.glow": """/* Segment Tree for Range Sum Query */
func build(arr, tree, node, start, endx):
    if start == endx:
        tree[node] = arr[start]
        return 0
    end
    let mid = (start + endx) / 2
    build(arr, tree, 2 * node, start, mid)
    build(arr, tree, 2 * node + 1, mid + 1, endx)
    tree[node] = tree[2 * node] + tree[2 * node + 1]
    return 0
end

func query(tree, node, start, endx, l, r):
    if r < start: return 0 end
    if endx < l: return 0 end
    if l <= start:
        if endx <= r:
            return tree[node]
        end
    end
    let mid = (start + endx) / 2
    let p1 = query(tree, 2 * node, start, mid, l, r)
    let p2 = query(tree, 2 * node + 1, mid + 1, endx, l, r)
    return p1 + p2
end

let arr = [1, 3, 5, 7, 9, 11]
let n = 6
let tree = range(4 * n)
build(arr, tree, 1, 0, n - 1)
show query(tree, 1, 0, n - 1, 1, 3)
""",

    "dsa_fenwicktree.glow": """/* Fenwick Tree BIT */
func update(BITree, n, index, val):
    index = index + 1
    while index <= n:
        BITree[index] = BITree[index] + val
        index = index + (index & (0 - index))
    end
end

func getSum(BITree, index):
    let sum = 0
    index = index + 1
    while index > 0:
        sum = sum + BITree[index]
        index = index - (index & (0 - index))
    end
    return sum
end

let freq = [2, 1, 1, 3, 2, 3, 4, 5, 6, 7, 8, 9]
let n = 12
let BITree = range(n + 1)
let i = 0
while i <= n:
    BITree[i] = 0
    i = i + 1
end

i = 0
while i < n:
    update(BITree, n, i, freq[i])
    i = i + 1
end

show getSum(BITree, 5)
""",

    "dsa_editdistance.glow": """/* Edit Distance */
func min3(a, b, c):
    let m = a
    if b < m: m = b end
    if c < m: m = c end
    return m
end

func editDistDP(str1, str2, m, n):
    let dp = range(m + 1)
    let i = 0
    while i <= m:
        let row = range(n + 1)
        let j = 0
        while j <= n: row[j] = 0 j = j + 1 end
        dp[i] = row
        i = i + 1
    end

    i = 0
    while i <= m:
        let j = 0
        while j <= n:
            let dp_i = dp[i]
            if i == 0:
                dp_i[j] = j
            end
            if i != 0:
                if j == 0:
                    dp_i[j] = i
                end
                if j != 0:
                    if str1[i - 1] == str2[j - 1]:
                        let dp_prev = dp[i - 1]
                        dp_i[j] = dp_prev[j - 1]
                    end
                    if str1[i - 1] != str2[j - 1]:
                        let dp_prev = dp[i - 1]
                        dp_i[j] = 1 + min3(dp_i[j - 1], dp_prev[j], dp_prev[j - 1])
                    end
                end
            end
            j = j + 1
        end
        i = i + 1
    end
    let final_row = dp[m]
    return final_row[n]
end

let str1 = [115, 117, 110, 100, 97, 121]
let str2 = [115, 97, 116, 117, 114, 100, 97, 121]
show editDistDP(str1, str2, 6, 8)
""",

    "dsa_subsetsum.glow": """/* Subset Sum Problem */
func isSubsetSum(set, n, sum):
    let subset = range(sum + 1)
    let i = 0
    while i <= sum:
        subset[i] = false
        i = i + 1
    end
    
    subset[0] = true
    
    i = 0
    while i < n:
        let j = sum
        while j >= set[i]:
            if subset[j - set[i]] == true:
                subset[j] = true
            end
            j = j - 1
        end
        i = i + 1
    end
    return subset[sum]
end

let set = [3, 34, 4, 12, 5, 2]
let sum = 9
show isSubsetSum(set, 6, sum)
""",

    "dsa_quicksort.glow": """/* QuickSort */
func partition(arr, low, high):
    let pivot = arr[high]
    let i = low - 1
    let j = low
    while j < high:
        if arr[j] < pivot:
            i = i + 1
            let temp = arr[i]
            arr[i] = arr[j]
            arr[j] = temp
        end
        j = j + 1
    end
    let temp2 = arr[i + 1]
    arr[i + 1] = arr[high]
    arr[high] = temp2
    return i + 1
end

func quickSort(arr, low, high):
    if low < high:
        let pi = partition(arr, low, high)
        quickSort(arr, low, pi - 1)
        quickSort(arr, pi + 1, high)
    end
end

let arr = [10, 7, 8, 9, 1, 5]
quickSort(arr, 0, 5)
show arr
""",

    "dsa_heapsort.glow": """/* HeapSort */
func heapify(arr, n, i):
    let largest = i
    let l = 2 * i + 1
    let r = 2 * i + 2

    if l < n:
        if arr[l] > arr[largest]:
            largest = l
        end
    end

    if r < n:
        if arr[r] > arr[largest]:
            largest = r
        end
    end

    if largest != i:
        let swap = arr[i]
        arr[i] = arr[largest]
        arr[largest] = swap
        heapify(arr, n, largest)
    end
end

func heapSort(arr, n):
    let i = n / 2 - 1
    while i >= 0:
        heapify(arr, n, i)
        i = i - 1
    end

    i = n - 1
    while i > 0:
        let temp = arr[0]
        arr[0] = arr[i]
        arr[i] = temp
        heapify(arr, i, 0)
        i = i - 1
    end
end

let arr = [12, 11, 13, 5, 6, 7]
heapSort(arr, 6)
show arr
""",

    "dsa_kmp.glow": """/* KMP String Matching */
func computeLPSArray(pat, M, lps):
    let length = 0
    lps[0] = 0
    let i = 1
    while i < M:
        if pat[i] == pat[length]:
            length = length + 1
            lps[i] = length
            i = i + 1
        end
        if pat[i] != pat[length]:
            if length != 0:
                length = lps[length - 1]
            end
            if length == 0:
                lps[i] = 0
                i = i + 1
            end
        end
    end
end

func KMPSearch(pat, txt, M, N):
    let lps = range(M)
    computeLPSArray(pat, M, lps)
    
    let i = 0
    let j = 0
    while i < N:
        if pat[j] == txt[i]:
            j = j + 1
            i = i + 1
        end
        if j == M:
            show "Found pattern at index ", i - j
            j = lps[j - 1]
        end
        if i < N:
            if pat[j] != txt[i]:
                if j != 0:
                    j = lps[j - 1]
                end
                if j == 0:
                    i = i + 1
                end
            end
        end
    end
end

let txt = [65, 66, 65, 66, 68, 65, 66, 65, 67, 68, 65, 66, 65, 66, 67]
let pat = [65, 66, 65, 66, 67]
KMPSearch(pat, txt, 5, 15)
""",

    "dsa_rabinkarp.glow": """/* Rabin-Karp String Matching */
func search(pat, txt, q, d, M, N):
    let p = 0
    let t = 0
    let h = 1
    let i = 0
    let j = 0

    i = 0
    while i < M - 1:
        h = (h * d) % q
        i = i + 1
    end

    i = 0
    while i < M:
        p = (d * p + pat[i]) % q
        t = (d * t + txt[i]) % q
        i = i + 1
    end

    i = 0
    while i <= N - M:
        if p == t:
            let match = true
            j = 0
            while j < M:
                if txt[i + j] != pat[j]:
                    match = false
                end
                j = j + 1
            end
            if match == true:
                show "Pattern found at index ", i
            end
        end
        if i < N - M:
            t = (d * (t - txt[i] * h) + txt[i + M]) % q
            if t < 0:
                t = t + q
            end
        end
        i = i + 1
    end
end

let txt = [71, 69, 69, 75, 83, 32, 70, 79, 82, 32, 71, 69, 69, 75, 83]
let pat = [71, 69, 69, 75, 83]
search(pat, txt, 101, 256, 5, 15)
""",

    "dsa_graph_dfs_bfs.glow": """/* Graph DFS & BFS */
func len2(arr):
    let count = 0
    for a in arr: count = count + 1 end
    return count
end

func BFS(graph, V, start):
    let visited = range(V)
    let i = 0
    while i < V: visited[i] = false i = i + 1 end
    
    let queue = [start]
    visited[start] = true
    
    let head = 0
    while head < len2(queue):
        let u = queue[head]
        head = head + 1
        show "BFS visited ", u
        
        let v = 0
        let row = graph[u]
        while v < V:
            if row[v] == 1:
                if visited[v] == false:
                    visited[v] = true
                    queue = queue + [v]
                end
            end
            v = v + 1
        end
    end
end

func DFSUtil(graph, V, v, visited):
    visited[v] = true
    show "DFS visited ", v
    let i = 0
    let row = graph[v]
    while i < V:
        if row[i] == 1:
            if visited[i] == false:
                DFSUtil(graph, V, i, visited)
            end
        end
        i = i + 1
    end
end

func DFS(graph, V, start):
    let visited = range(V)
    let i = 0
    while i < V: visited[i] = false i = i + 1 end
    DFSUtil(graph, V, start, visited)
end

let graph = [
    [0, 1, 1, 0],
    [0, 0, 1, 0],
    [1, 0, 0, 1],
    [0, 0, 0, 1]
]
show "BFS:"
BFS(graph, 4, 2)
show "DFS:"
DFS(graph, 4, 2)
"""
}

os.makedirs("benchmarks", exist_ok=True)
for name, content in glow_files.items():
    with open(os.path.join("benchmarks", name), "w") as f:
        f.write(content)
