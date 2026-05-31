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
            b = board[i]
            if b == col:
                return False
            diff = abs(col - b)
            if diff == (row - i):
                return False
        return True

nq = NQueens(10)
print(nq.solve())
