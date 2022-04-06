#!/usr/bin/perl
=head1 NAME

gen-zone-abbrevs.pl - generate list of timezones

=head1 SYNOPSIS

  gen-zone-abbrevs.pl [zone-abbrevs.txt [main.zi]] > timezones.h

Options:

  first argument is zone-abbrevs.txt or it's equivalent
  second argument is main.zi of it's equivalent

=head1 DESCRIPTION

Reads input description of known IANA timezone abbreviations (including single-
letter military zone names, e.g. Zulu 'Z'), and list of zones from Olson/tzcode
main.zi file, and then generates set of macro calls defining timezone with all
attributes.

  ZONE_ABBREV(  25,    0, "Z", TZ_MILITARY)
  ZONE_ABBREV( 112,    0, "UT", TZ_UTC|TZ_RFC)
  ZONE_ABBREV( 128,    0, "ACT", TZ_AMBIGUOUS)
  ZONE_ABBREV( 237,  240, "MSD", 0)
  ZONE_ABBREV( 238,  180, "MSK", 0)

  ZONE_UNIQUE(1000, "Etc/GMT")
  ZONE_UNIQUE(1001, "Etc/UTC")
  ZONE_UNIQUE(1030, "Europe/Minsk")
  ZONE_UNIQUE(1032, "Europe/Moscow")

One may use this generated file directly (via include in C/C++) or generate
different artifact which could be used from other language (i.e. Go, or Python).
We guarantee that constants assigned to particular zone will be maintained in
future. To simplify future updates we leave some holes in list.

NB! In a future, for the next versions of Olson/tzdata database, we plan to
automate updates of generated list. At the moment we leave it as manual process.

=cut

use strict;
use warnings;

my $next_id    = 0;
my $last_first = undef;

# simply increment id for each next string passed
# but jump to the nearest modulo 16 number if there
# was first character changed - to make possible
# manual inserts in future.
sub next_id {
    my ($string) = @_;
    my $first = substr( $string, 0, 1 );
    $next_id++;
    return $next_id if length($string) == 1;    # sequential military zones
    return $next_id if defined $last_first and $first eq $last_first;
    $last_first = $first;
    $next_id += 7;
    $next_id &= ~0x7;   # jump to the next modulo of 8 for the next alpha letter
    return $next_id;
}

sub encode {
    my ($string) = @_;
    my $v = 0;
    foreach my $c (split //, $string) {
        $v = ($v << 5) | ((ord($c) | 0x20) ^ 0x60);
    }
    return $v;
}

my %Universal = (
    GMT => 0,
    UTC => 0,
    UT  => 0,
);

my %Military = (
    A =>   1*60, B =>   2*60, C =>   3*60,
    D =>   4*60, E =>   5*60, F =>   6*60,
    G =>   7*60, H =>   8*60, I =>   9*60,
    K =>  10*60, L =>  11*60, M =>  12*60,

    N =>  -1*60, O =>  -2*60, P =>  -3*60,
    Q =>  -4*60, R =>  -5*60, S =>  -6*60,
    T =>  -7*60, U =>  -8*60, V =>  -9*60,
    W => -10*60, X => -11*60, Y => -12*60,

    Z => 0,
);

my %Rfc = (
    UT  => 0,
    UTC => 0,
    GMT => 0,
    EST => -5*60, EDT => -4*60,
    CST => -6*60, CDT => -5*60,
    MST => -7*60, MDT => -6*60,
    PST => -8*60, PDT => -7*60,
);

my %FlagName;

BEGIN {
    my %flags = (
        TZ_UTC       => 0x01,
        TZ_RFC       => 0x02,
        TZ_MILITARY  => 0x04,
        TZ_AMBIGUOUS => 0x08,
        TZ_NYI       => 0x10,
    );
    require constant; constant->import(\%flags);
    %FlagName = reverse %flags;
}

my $zone_abbrevs_file = shift || 'zone-abbrevs.txt';
my $tzdata_zi_file    = shift || 'main.zi';

# ACT    -05:00          # Acre Time (South America)
# ACT    +10:30 / +09:30 # Australian Central Time (Australia)
# ACWST  +08:45          # Australian Central Western Standard Time (Australia)

my $EntryRx = do {
    my $name   = '[A-Z][A-Za-z]{0,5}';
    my $offset = '[+-] (?: [01][0-9] | [2][0-3]) [:] [0-5][0-9]';
    qr< ($name) \s+ ($offset) (?: \s+ / \s+ ($offset))? \s+ #>xo
};

my %ZoneAbbrevs;

# read zone-abbrevs.txt with definition of all currently known
# timezone abbreviations.
sub read_abbrevs_file($) {
    my $filename = shift;
    open( my $fh, '<:raw', $filename )
      or die qq<Could not open '$filename' for reading: '$!'>;

    while (<$fh>) {
        next if /\A \s* \# /x;

        my ( $name, $offset1, $offset2 ) = m< \A $EntryRx >x
          or die qq/Could not parse zone entry at line $_: $./;

        my $encoded = encode($name);
        my $offset  = 0;
        my $flags   = 0;

        if ($offset2) {
            $flags = TZ_AMBIGUOUS;
        }
        else {
            my ( $h, $m ) = split /[:]/, $offset1;
            $offset = $h * 60 + $m;
        }

        if ( exists $ZoneAbbrevs{$encoded} ) {
            $flags |= TZ_AMBIGUOUS;
            $offset = 0;
        }

        if ( exists $Universal{$name} ) {
            $flags |= TZ_UTC;
            $offset = 0;
        }

        if ( exists $Military{$name} ) {
            $flags |= TZ_MILITARY;
            $offset = $Military{$name};
        }

        if ( exists $Rfc{$name} ) {
            $flags |= TZ_RFC;
            $offset = $Rfc{$name};
        }

        $ZoneAbbrevs{$encoded} = [ $flags, $offset, $name ];
    }
}

my %ZoneNames;

sub read_zi_file($) {
    my $filename = shift;
    open( my $fh, '<:raw', $filename )
      or die qq<Could not open '$filename' for reading: '$!'>;

    my $zone;
    my $format;
    while (<$fh>) {
        next if /^#/;
        if (/^Zone\s+([A-Za-z\-\_\/]+)\s+/) {
            $zone   = $1;
            $ZoneNames{$zone}++ if $zone =~ q|/|;
            next;
        }
    }

}

sub gen_header() {
    printf "/* Automatically generated by gen-zone-abbrevs.pl */\n";

    # first we generate abbreviations list
    foreach my $encoded ( sort { $a <=> $b } keys %ZoneAbbrevs ) {
        my ( $flags, $offset, $name ) = @{ $ZoneAbbrevs{$encoded} };
        my $nextid = next_id($name);

        if ( $flags != 0 ) {
            my @names;

            while ( ( my $flag = $flags & -$flags ) != 0 ) {
                push @names, $FlagName{$flag} || die sprintf '%4d', $flag;
                $flags &= ~$flag;
            }

            $flags = join '|', @names;
        }

        printf "ZONE_ABBREV(%4d, %4d, \"%s\", %s)\n",
          $nextid, $offset, $name, $flags;
    }

    # second we enumerate all known from main.zi primary zone names
    foreach my $zonename (sort keys %ZoneNames) {
        my $nextid = next_id($zonename);
        printf "ZONE_UNIQUE(%4d, \"%s\")\n", $nextid, $zonename;
    }
}

read_abbrevs_file($zone_abbrevs_file);
read_zi_file($tzdata_zi_file);
gen_header();
