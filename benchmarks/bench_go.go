package main

import (
	"fmt"
	"time"
)

func compute_pi(iterations int) float64 {
	pi := 0.0
	divisor := 1.0
	sign := 1.0
	for i := 0; i < iterations; i++ {
		pi += sign * (4.0 / divisor)
		divisor += 2.0
		sign *= -1.0
	}
	return pi
}

func matrix_multiply_sim(size int) float64 {
	total := 0.0
	for i := 0; i < size; i++ {
		for j := 0; j < size; j++ {
			for k := 0; k < size; k++ {
				total += float64(i*j*k) * 0.0001
			}
		}
	}
	return total
}

func fib(n int) int {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func loop100m() int64 {
	s := int64(0)
	for i := 0; i < 100000000; i++ {
		s += int64(i)
	}
	return s
}

func main() {
	fmt.Println("Starting Go Benchmarks")

	start := time.Now()
	compute_pi(1000000)
	fmt.Printf("Pi (1M): %fs\n", time.Since(start).Seconds())

	start = time.Now()
	matrix_multiply_sim(200)
	fmt.Printf("Matrix (200^3): %fs\n", time.Since(start).Seconds())

	start = time.Now()
	total_fib := 0
	for i := 0; i < 30; i++ {
		total_fib += fib(i)
	}
	fmt.Printf("Fib(30): %fs\n", time.Since(start).Seconds())

	start = time.Now()
	loop100m()
	fmt.Printf("Loop (100M): %fs\n", time.Since(start).Seconds())
}
