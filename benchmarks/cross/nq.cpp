#include <iostream>
#include <vector>

class NQueens {
public:
    int n;
    int count;

    NQueens(int n) : n(n), count(0) {}

    int solve() {
        std::vector<int> board(n, -1);
        backtrack(board, 0);
        return count;
    }

private:
    void backtrack(std::vector<int>& board, int row) {
        if (row == n) {
            count++;
            return;
        }
        for (int col = 0; col < n; col++) {
            if (is_safe(board, row, col)) {
                board[row] = col;
                backtrack(board, row + 1);
                board[row] = -1;
            }
        }
    }

    bool is_safe(const std::vector<int>& board, int row, int col) {
        for (int i = 0; i < row; i++) {
            int b = board[i];
            if (b == col) return false;
            int diff = col - b;
            if (diff < 0) diff = -diff;
            if (diff == (row - i)) return false;
        }
        return true;
    }
};

int main() {
    NQueens nq(10);
    std::cout << nq.solve() << std::endl;
    return 0;
}
