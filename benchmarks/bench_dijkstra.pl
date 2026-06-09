my $V = 1000;
my $INF = 999999;
sub minDistance {
    my ($dist, $sptSet) = @_;
    my $min_val = $INF;
    my $min_index = -1;
    for (my $v = 0; $v < $V; $v++) {
        if (!$sptSet->[$v] && $dist->[$v] <= $min_val) {
            $min_val = $dist->[$v];
            $min_index = $v;
        }
    }
    return $min_index;
}
sub dijkstra {
    my ($graph, $src) = @_;
    my @dist = ($INF) x $V;
    my @sptSet = (0) x $V;
    $dist[$src] = 0;
    for (my $count = 0; $count < $V - 1; $count++) {
        my $u = minDistance(\@dist, \@sptSet);
        $sptSet[$u] = 1;
        my $row = $graph->[$u];
        for (my $v = 0; $v < $V; $v++) {
            if (!$sptSet[$v] && $row->[$v] != 0 && $dist[$u] != $INF && $dist[$u] + $row->[$v] < $dist[$v]) {
                $dist[$v] = $dist[$u] + $row->[$v];
            }
        }
    }
}
my @graph;
for (my $i = 0; $i < $V; $i++) {
    my @row;
    for (my $j = 0; $j < $V; $j++) {
        if ($i == $j) {
            $row[$j] = 0;
        } else {
            $row[$j] = ($i * $j + 1) % 100;
        }
    }
    $graph[$i] = \@row;
}
dijkstra(\@graph, 0);
print "Done\n";
