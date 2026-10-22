fn main() {
    let mut sum = 0i64;
    for i in 0..100000 {
        sum += i;
    }
    println!("{}", sum);
}