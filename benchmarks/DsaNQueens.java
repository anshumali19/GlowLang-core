public class DsaNQueens {
    static boolean isSafe(int[] board, int row, int col) {
        for (int i = 0; i < row; i++) {
            if (board[i] == col || Math.abs(board[i] - col) == Math.abs(i - row))
                return false;
        }
        return true;
    }
    static int nqCount = 0;
    static void backtrack(int[] board, int row, int n) {
        if (row == n) {
            nqCount++;
            return;
        }
        for (int col = 0; col < n; col++) {
            if (isSafe(board, row, col)) {
                board[row] = col;
                backtrack(board, row + 1, n);
                board[row] = -1;
            }
        }
    }
    public static void main(String[] args) {
        int n = 11;
        int[] board = new int[n];
        for (int i = 0; i < n; i++) board[i] = -1;
        backtrack(board, 0, n);
        System.out.println(nqCount);
    }
}
