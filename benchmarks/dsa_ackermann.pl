no warnings 'recursion';
sub ack {
    my ($m, $n) = @_;
    return $n + 1 if $m == 0;
    return ack($m - 1, 1) if $n == 0;
    return ack($m - 1, ack($m, $n - 1));
}
print ack(3, 8) . "\n";
