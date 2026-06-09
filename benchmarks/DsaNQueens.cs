using System;
class DsaNQueens {
    static bool IsSafe(int[] board, int row, int col, int n) {
        for (int i = 0; i < col; i++) {
            if (board[i] == row) return false;
            if (board[i] - i == row - col) return false;
            if (board[i] + i == row + col) return false;
        }
        return true;
    }
    static int Solve(int[] board, int col, int n) {
        if (col >= n) return 1;
        int count = 0;
        for (int row = 0; row < n; row++) {
            if (IsSafe(board, row, col, n)) {
                board[row] = col;
                count += Solve(board, col + 1, n);
            }
        }
        return count;
    }
    static void Main() {
        int n = 12;
        int[] board = new int[n];
        Console.WriteLine(Solve(board, 0, n));
    }
}
