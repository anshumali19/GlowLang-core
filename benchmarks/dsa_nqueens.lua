local nq_count = 0
local math_abs = math.abs

local function is_safe(board, row, col)
    for i = 0, row - 1 do
        if board[i] == col or math_abs(board[i] - col) == math_abs(i - row) then
            return false
        end
    end
    return true
end

local function backtrack(board, row, n)
    if row == n then
        nq_count = nq_count + 1
        return
    end
    for col = 0, n - 1 do
        if is_safe(board, row, col) then
            board[row] = col
            backtrack(board, row + 1, n)
            board[row] = -1
        end
    end
end

local n = 11
local board = {}
for i = 0, n - 1 do board[i] = -1 end
backtrack(board, 0, n)
print(nq_count)
