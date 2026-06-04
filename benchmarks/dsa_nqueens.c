#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int is_safe(int *board, int row, int col) {
    for (int i = 0; i < row; i++) {
        if (board[i] == col || abs(board[i] - col) == abs(i - row))
            return 0;
    }
    return 1;
}
int nq_count = 0;
void backtrack(int *board, int row, int n) {
    if (row == n) {
        nq_count++;
        return;
    }
    for (int col = 0; col < n; col++) {
        if (is_safe(board, row, col)) {
            board[row] = col;
            backtrack(board, row + 1, n);
            board[row] = -1;
        }
    }
}
int main() {
    int n = 11;
    int *board = (int *)malloc(sizeof(int) * n);
    for (int i = 0; i < n; i++) board[i] = -1;
    backtrack(board, 0, n);
    printf("%d\n", nq_count);
    free(board);
    return 0;
}
