fn fib(n: i32) -> i32 {
    if n < 2 { return n; }
    fib(n - 1) + fib(n - 2)
}

fn main() {
    let mut total = 0;
    for i in 0..=30 {
        total += fib(i);
    }
    println!("{}", total);
}