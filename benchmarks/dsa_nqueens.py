def isSafe(board, row, col, n):
    for i in range(col):
        if board[i] == row:
            return False
        if board[i] - i == row - col:
            return False
        if board[i] + i == row + col:
            return False
    return True

def solve(board, col, n):
    if col >= n:
        return 1
    count = 0
    for row in range(n):
        if isSafe(board, row, col, n):
            board[row] = col
            count += solve(board, col + 1, n)
    return count

n = 8
board = [0] * n
print(solve(board, 0, n))
