using System;
using System.Diagnostics;
using System.Collections.Generic;

class NQueens {
    public int n;
    public int count;

    public NQueens(int n) {
        this.n = n;
        this.count = 0;
    }

    public bool IsSafe(List<int> board, int row, int col) {
        for (int i = 0; i < row; i++) {
            if (board[i] == col || Math.Abs(board[i] - col) == Math.Abs(i - row))
                return false;
        }
        return true;
    }

    public void Backtrack(List<int> board, int row) {
        if (row == n) {
            count++;
            return;
        }
        for (int col = 0; col < n; col++) {
            if (IsSafe(board, row, col)) {
                board[row] = col;
                Backtrack(board, row + 1);
                board[row] = -1;
            }
        }
    }

    static void Main() {
        int n = 11;
        NQueens nq = new NQueens(n);
        List<int> board = new List<int>();
        for(int i=0; i<n; i++) board.Add(-1);

        Stopwatch sw = Stopwatch.StartNew();
        nq.Backtrack(board, 0);
        sw.Stop();

        Console.WriteLine($"N-Queens ({n}): {nq.count} found in {sw.Elapsed.TotalSeconds:F4}s");
    }
}
