#!/usr/bin/perl -s

use strict;
use warnings;
use IPC::Open2 qw/open2/;

our ($exe);
die "$0: no valid -exe given" unless -f $exe;

our($out, $in);
my $addr2line_pid = open2($in, $out, qw/addr2line -f -e/, $exe) or die "can't spawn addr2line: $!";

sub addr_to_name {
    my ($f) = @_;

    print $out "$f\n";
    my $name = <$in>;
    my $junk = <$in>;

    chomp $name;
    return $name;
}

while (<>) {
    chomp;
    next unless /(.*)(E|X)(0x\w+)/;

    my $f = addr_to_name($3);

    print "$1$2 $f\n";
}
