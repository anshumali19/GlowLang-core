package main
import "fmt"
func is_safe(board []int, row, col int) bool {
    for i := 0; i < row; i++ {
        diff := col - board[i]
        if diff < 0 { diff = -diff }
        if board[i] == col || diff == (row - i) {
            return false
        }
    }
    return true
}
var nq_count = 0
func backtrack(board []int, row, n int) {
    if row == n {
        nq_count++
        return
    }
    for col := 0; col < n; col++ {
        if is_safe(board, row, col) {
            board[row] = col
            backtrack(board, row+1, n)
            board[row] = -1
        }
    }
}
func main() {
    n := 11
    board := make([]int, n)
    for i := range board { board[i] = -1 }
    backtrack(board, 0, n)
    fmt.Println(nq_count)
}
