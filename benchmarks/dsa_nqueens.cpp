#include <iostream>
using namespace std;

bool isSafe(int *board, int row, int col, int n) {
    for (int i = 0; i < col; i++) {
        if (board[i] == row) return false;
        if (board[i] - i == row - col) return false;
        if (board[i] + i == row + col) return false;
    }
    return true;
}

int solve(int *board, int col, int n) {
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

int main() {
    int n = 8;
    int board[12] = {0};
    cout << solve(board, 0, n) << endl;
    return 0;
}
