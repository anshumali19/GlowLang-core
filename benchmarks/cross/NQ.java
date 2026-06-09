public class NQ {
    private int n;
    private int count;

    public NQ(int n) {
        this.n = n;
        this.count = 0;
    }

    public int solve() {
        int[] board = new int[n];
        for (int i = 0; i < n; i++) board[i] = -1;
        backtrack(board, 0);
        return count;
    }

    private void backtrack(int[] board, int row) {
        if (row == n) {
            count++;
            return;
        }
        for (int col = 0; col < n; col++) {
            if (isSafe(board, row, col)) {
                board[row] = col;
                backtrack(board, row + 1);
                board[row] = -1;
            }
        }
    }

    private boolean isSafe(int[] board, int row, int col) {
        for (int i = 0; i < row; i++) {
            int b = board[i];
            if (b == col) return false;
            int diff = Math.abs(col - b);
            if (diff == (row - i)) return false;
        }
        return true;
    }

    public static void main(String[] args) {
        NQ nq = new NQ(10);
        System.out.println(nq.solve());
    }
}
