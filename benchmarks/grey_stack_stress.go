package main

import (
	"fmt"
	"runtime"
)

type Node struct {
	Next *Node
}

func main() {
	depth := 500000
	head := &Node{}
	curr := head

	for i := 0; i < depth; i++ {
		curr.Next = &Node{}
		curr = curr.Next
	}

	fmt.Println("Built linked list of depth:", depth)

	// Force GC
	runtime.GC()

	fmt.Println("Golang GC completed successfully without segfaulting!")
}
