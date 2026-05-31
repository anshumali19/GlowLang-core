#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int n;
    int count;
} NQueens;

int is_safe(int* board, int row, int col) {
    for (int i = 0; i < row; i++) {
        int b = board[i];
        if (b == col) return 0;
        int diff = col - b;
        if (diff < 0) diff = -diff;
        if (diff == (row - i)) return 0;
    }
    return 1;
}

void backtrack(NQueens* nq, int* board, int row) {
    if (row == nq->n) {
        nq->count++;
        return;
    }
    for (int col = 0; col < nq->n; col++) {
        if (is_safe(board, row, col)) {
            board[row] = col;
            backtrack(nq, board, row + 1);
            board[row] = -1;
        }
    }
}

int solve(int n) {
    NQueens nq;
    nq.n = n;
    nq.count = 0;
    int* board = (int*)malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) board[i] = -1;
    backtrack(&nq, board, 0);
    int result = nq.count;
    free(board);
    return result;
}

int main() {
    printf("%d\n", solve(10));
    return 0;
}
