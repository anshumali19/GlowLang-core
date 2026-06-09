package main

import "fmt"

func fib(n int64) int64 {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	var total int64 = 0
	var i int64
	for i = 0; i < 30; i++ {
		total += fib(i)
	}
	fmt.Println(total)
}
