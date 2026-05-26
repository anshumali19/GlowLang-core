import time

class NQueens:
    def __init__(self, n):
        self.n = n
        self.count = 0

    def solve(self):
        board = [-1] * self.n
        self.backtrack(board, 0)
        return self.count

    def backtrack(self, board, row):
        if row == self.n:
            self.count += 1
            return
        for col in range(self.n):
            if self.is_safe(board, row, col):
                board[row] = col
                self.backtrack(board, row + 1)
                board[row] = -1

    def is_safe(self, board, row, col):
        for i in range(row):
            if board[i] == col or \
               abs(board[i] - col) == abs(i - row):
                return False
        return True

n = 11
start = time.time()
nq = NQueens(n)
solutions = nq.solve()
end = time.time()
print(f"N-Queens ({n}): {solutions} found in {end - start:.4f}s")
