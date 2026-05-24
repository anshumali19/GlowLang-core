package main

import "fmt"

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	i, total := 0, 0
	for i < 20 {
		total += fib(i)
		i++
	}
	fmt.Println(total)
}
