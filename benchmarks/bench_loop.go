package main
import "fmt"
func main() {
    var total int64 = 0
    for i := 0; i < 10000000; i++ { total += int64(i) }
    fmt.Println(total)
}
