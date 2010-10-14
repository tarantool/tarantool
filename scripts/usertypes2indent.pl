#!/usr/bin/perl -w

use strict;

$/=undef;

$_ = <>;

my $brre;

$brre = qr/(?:[^{}]*|\{(??{$brre})\})*/;

my @a = /typedef\s+(?:(?:enum|struct|union)\s+[^{};]*\{$brre\}\s*|(?:\w+\s+)+)(\w+)[^;{}]*;/g;

foreach (@a) {
	print "-T $_ ";
}
