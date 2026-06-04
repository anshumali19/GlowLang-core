my $nq_count = 0;
sub is_safe {
    my ($board, $row, $col) = @_;
    for (my $i = 0; $i < $row; $i++) {
        my $b = $board->[$i];
        if ($b == $col || abs($b - $col) == abs($i - $row)) {
            return 0;
        }
    }
    return 1;
}
sub backtrack {
    my ($board, $row, $n) = @_;
    if ($row == $n) {
        $nq_count++;
        return;
    }
    for (my $col = 0; $col < $n; $col++) {
        if (is_safe($board, $row, $col)) {
            $board->[$row] = $col;
            backtrack($board, $row + 1, $n);
            $board->[$row] = -1;
        }
    }
}
my $n = 11;
my @board = (-1) x $n;
backtrack(\@board, 0, $n);
print "$nq_count\n";
