public class DsaNQueens {
    static boolean isSafe(int[] board, int row, int col, int n) {
        for (int i = 0; i < col; i++) {
            if (board[i] == row) return false;
            if (board[i] - i == row - col) return false;
            if (board[i] + i == row + col) return false;
        }
        return true;
    }
    static int solve(int[] board, int col, int n) {
        if (col >= n) return 1;
        int count = 0;
        for (int row = 0; row < n; row++) {
            if (isSafe(board, row, col, n)) {
                board[row] = col;
                count += solve(board, col + 1, n);
            }
        }
        return count;
    }
    public static void main(String[] args) {
        int n = 8;
        int[] board = new int[n];
        System.out.println(solve(board, 0, n));
    }
}
