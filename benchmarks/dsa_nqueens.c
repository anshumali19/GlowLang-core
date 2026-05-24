#include <stdio.h>

int isSafe(int *board, int row, int col, int n) {
    int i;
    for (i = 0; i < col; i++) {
        if (board[i] == row) return 0;
        if (board[i] - i == row - col) return 0;
        if (board[i] + i == row + col) return 0;
    }
    return 1;
}

int solve(int *board, int col, int n) {
    if (col >= n) return 1;
    int count = 0, row;
    for (row = 0; row < n; row++) {
        if (isSafe(board, row, col, n)) {
            board[row] = col;
            count += solve(board, col + 1, n);
        }
    }
    return count;
}

int main() {
    int n = 8;
    int board[12] = {0};
    int result = solve(board, 0, n);
    printf("%d\n", result);
    return 0;
}
