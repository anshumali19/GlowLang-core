package main

import "fmt"

type NQueens struct {
	n     int
	count int
}

func (nq *NQueens) isSafe(board []int, row, col int) bool {
	for i := 0; i < row; i++ {
		b := board[i]
		if b == col {
			return false
		}
		diff := col - b
		if diff < 0 {
			diff = -diff
		}
		if diff == (row - i) {
			return false
		}
	}
	return true
}

func (nq *NQueens) backtrack(board []int, row int) {
	if row == nq.n {
		nq.count++
		return
	}
	for col := 0; col < nq.n; col++ {
		if nq.isSafe(board, row, col) {
			board[row] = col
			nq.backtrack(board, row+1)
			board[row] = -1
		}
	}
}

func (nq *NQueens) solve() int {
	board := make([]int, nq.n)
	for i := range board {
		board[i] = -1
	}
	nq.backtrack(board, 0)
	return nq.count
}

func main() {
	nq := &NQueens{n: 10}
	fmt.Println(nq.solve())
}
