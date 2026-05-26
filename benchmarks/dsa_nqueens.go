package main

import (
	"fmt"
	"math"
	"time"
)

type NQueens struct {
	n     int
	count int
}

func isSafe(board []int, row, col int) bool {
	for i := 0; i < row; i++ {
		if board[i] == col || math.Abs(float64(board[i]-col)) == math.Abs(float64(i-row)) {
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
		if isSafe(board, row, col) {
			board[row] = col
			nq.backtrack(board, row+1)
			board[row] = -1
		}
	}
}

func main() {
	n := 11
	nq := &NQueens{n: n}
	board := make([]int, n)
	for i := range board {
		board[i] = -1
	}

	start := time.Now()
	nq.backtrack(board, 0)
	elapsed := time.Since(start)

	fmt.Printf("N-Queens (%d): %d found in %.4fs\n", n, nq.count, elapsed.Seconds())
}
