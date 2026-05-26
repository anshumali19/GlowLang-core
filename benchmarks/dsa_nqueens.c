#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

typedef struct {
    int n;
    int count;
} NQueens;

int is_safe(int *board, int row, int col) {
    for (int i = 0; i < row; i++) {
        if (board[i] == col || abs(board[i] - col) == abs(i - row))
            return 0;
    }
    return 1;
}

void backtrack(NQueens *nq, int *board, int row) {
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

int main() {
    int n = 11;
    NQueens nq = {n, 0};
    int *board = malloc(sizeof(int) * n);
    for(int i=0; i<n; i++) board[i] = -1;
    
    clock_t start = clock();
    backtrack(&nq, board, 0);
    clock_t end = clock();
    
    printf("N-Queens (%d): %d found in %.4fs\n", n, nq.count, (double)(end - start) / CLOCKS_PER_SEC);
    return 0;
}
