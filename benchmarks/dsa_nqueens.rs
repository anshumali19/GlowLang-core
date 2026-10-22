fn solve(n: usize, row: usize, cols: &mut [bool], d1: &mut [bool], d2: &mut [bool]) -> u64 {
    if row == n { return 1; }
    let mut count = 0;
    for col in 0..n {
        if !cols[col] && !d1[row + col] && !d2[row + n - 1 - col] {
            cols[col] = true;
            d1[row + col] = true;
            d2[row + n - 1 - col] = true;
            count += solve(n, row + 1, cols, d1, d2);
            cols[col] = false;
            d1[row + col] = false;
            d2[row + n - 1 - col] = false;
        }
    }
    count
}

fn main() {
    let n = 12;
    let mut cols = vec![false; n];
    let mut d1 = vec![false; 2 * n];
    let mut d2 = vec![false; 2 * n];
    println!("{}", solve(n, 0, &mut cols, &mut d1, &mut d2));
}