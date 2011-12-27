#!/usr/bin/perl

use strict;
use warnings;
my $log = shift or die "usage: $0 some.log\n";
my $reopen;
$SIG{HUP} = sub { $reopen = 1 };

my $fh;
while (<STDIN>) {
    if ($reopen or not $fh) {
        undef $fh;
        undef $reopen;
        if (open $fh, ">>", $log) {
            select $fh;
            $| = 1;
        }
    }

    print;
}
