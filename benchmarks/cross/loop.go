package main

import "fmt"

func main() {
	var i int64 = 0
	var total int64 = 0
	for i < 1000000 {
		total += i
		i++
	}
	fmt.Println(total)
}
