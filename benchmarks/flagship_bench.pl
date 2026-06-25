use strict;
use warnings;
use Time::HiRes qw(gettimeofday tv_interval);

my $start = [gettimeofday];

my @vec_a = ((1.5) x 16);
my @vec_b = ((2.0) x 16);
my $img_hdr = pack("C*", 0x89, 0x50, 0x4E, 0x47, ((0) x 12), 0, 0, 7, 0x80, 0, 0, 4, 0x38);

sub blake_mix {
    my ($str) = @_;
    my $h = 0xCBF29CE4; # 32-bit sim for Perl
    foreach my $ch (split //, $str) {
        $h ^= ord($ch);
        $h = ($h * 33) & 0xFFFFFFFF;
    }
    return $h;
}

my $valid = 0;
for (1..50000) {
    # 1. Regex json scan
    my $json = '{"txn_id": "SOV-994821"}';
    $json =~ /"txn_id":\s*"([^"]+)"/;
    my $val = $1;

    # 2. Crypto
    my $h = blake_mix($val);

    # 3. Vector dot
    my $dot = 0;
    for my $i (0..15) { $dot += $vec_a[$i] * $vec_b[$i]; }

    # 4. Binary unpack
    my ($w, $h_img) = unpack("N N", substr($img_hdr, 16, 8));

    if ($dot == 48.0 && $w == 1920) { $valid++; }
}

my $dur = tv_interval($start);
print "=== PERL 5 BENCHMARK RESULTS ===\n";
print "Iterations:  50,000\n";
print "Correctness: $valid / 50000 (100%)\n";
printf "Time Taken:  %.6f seconds\n", $dur;
