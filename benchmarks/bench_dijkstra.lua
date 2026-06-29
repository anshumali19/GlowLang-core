local V = 1000
local INF = 999999

local function minDistance(dist, sptSet)
    local min_val = INF
    local min_index = -1
    for v = 0, V - 1 do
        if not sptSet[v] and dist[v] <= min_val then
            min_val = dist[v]
            min_index = v
        end
    end
    return min_index
end

local function dijkstra(graph, src)
    local dist = {}
    local sptSet = {}
    for i = 0, V - 1 do
        dist[i] = INF
        sptSet[i] = false
    end
    dist[src] = 0
    for i = 0, V - 2 do
        local u = minDistance(dist, sptSet)
        sptSet[u] = true
        for v = 0, V - 1 do
            if not sptSet[v] and graph[u][v] ~= 0 and dist[u] ~= INF and dist[u] + graph[u][v] < dist[v] then
                dist[v] = dist[u] + graph[u][v]
            end
        end
    end
end

local graph = {}
for i = 0, V - 1 do
    graph[i] = {}
    for j = 0, V - 1 do
        if i == j then graph[i][j] = 0
        else graph[i][j] = (i * j + 1) % 100 end
    end
end
dijkstra(graph, 0)
print("Done")
