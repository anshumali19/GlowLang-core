function isSafe(board, row, col) {
    for (let i = 0; i < row; i++) {
        if (board[i] === col || Math.abs(board[i] - col) === Math.abs(i - row))
            return false;
    }
    return true;
}
let nq_count = 0;
function backtrack(board, row, n) {
    if (row === n) {
        nq_count++;
        return;
    }
    for (let col = 0; col < n; col++) {
        if (isSafe(board, row, col)) {
            board[row] = col;
            backtrack(board, row + 1, n);
            board[row] = -1;
        }
    }
}
const n = 11;
let board = new Array(n).fill(-1);
backtrack(board, 0, n);
console.log(nq_count);
