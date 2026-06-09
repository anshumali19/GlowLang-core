sub fib {
    my ($n) = @_;
    return $n if $n < 2;
    return fib($n - 1) + fib($n - 2);
}
my $total = 0;
for (my $i = 0; $i <= 30; $i++) { $total += fib($i); }
print "$total\n";
