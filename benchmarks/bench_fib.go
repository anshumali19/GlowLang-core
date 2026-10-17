package main

func fib(n int) int {
    if n < 2 { return n }
    return fib(n-1) + fib(n-2)
}

func main() {
    total := 0
    for i := 0; i <= 30; i++ {
        total += fib(i)
    }
    println(total)
}