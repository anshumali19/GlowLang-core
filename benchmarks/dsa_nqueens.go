package main

import "fmt"

func isSafe(board []int, row, col, n int) bool {
	for i := 0; i < col; i++ {
		if board[i] == row {
			return false
		}
		if board[i]-i == row-col {
			return false
		}
		if board[i]+i == row+col {
			return false
		}
	}
	return true
}

func solve(board []int, col, n int) int {
	if col >= n {
		return 1
	}
	count := 0
	for row := 0; row < n; row++ {
		if isSafe(board, row, col, n) {
			board[row] = col
			count += solve(board, col+1, n)
		}
	}
	return count
}

func main() {
	n := 12
	board := make([]int, n)
	fmt.Println(solve(board, 0, n))
}
