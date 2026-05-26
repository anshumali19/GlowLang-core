#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>

class NQueens {
public:
    int n;
    int count;

    NQueens(int n) : n(n), count(0) {}

    bool is_safe(const std::vector<int>& board, int row, int col) {
        for (int i = 0; i < row; ++i) {
            if (board[i] == col || std::abs(board[i] - col) == std::abs(i - row))
                return false;
        }
        return true;
    }

    void backtrack(std::vector<int>& board, int row) {
        if (row == n) {
            count++;
            return;
        }
        for (int col = 0; col < n; ++col) {
            if (is_safe(board, row, col)) {
                board[row] = col;
                backtrack(board, row + 1);
                board[row] = -1;
            }
        }
    }
};

int main() {
    int n = 11;
    NQueens nq(n);
    std::vector<int> board(n, -1);

    auto start = std::chrono::high_resolution_clock::now();
    nq.backtrack(board, 0);
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> elapsed = end - start;
    std::cout << "N-Queens (" << n << "): " << nq.count << " found in " << elapsed.count() << "s" << std::endl;

    return 0;
}
