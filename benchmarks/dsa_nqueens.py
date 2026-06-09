def is_safe(board, row, col):
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
