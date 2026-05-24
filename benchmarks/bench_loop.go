package main

import "fmt"

func main() {
	i, total := 0, 0
	for i < 100000 {
		total += i
		i++
	}
	fmt.Println(total)
}
