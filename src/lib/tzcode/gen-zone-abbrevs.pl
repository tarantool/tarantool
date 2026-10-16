#!/usr/bin/perl
=head1 NAME

gen-zone-abbrevs.pl - generate list of timezones

=head1 SYNOPSIS

  gen-zone-abbrevs.pl [zone-abbrevs.txt [main.zi [timezones.h [timezones.lua]]]]

Options:

  first argument is zone-abbrevs.txt or it's equivalent
  second argument is main.zi or it's equivalent
  third is a name of output header file (timezones.h by default)
  fourth is a name of output lua config (timezones.lua by default)

=head1 DESCRIPTION

Reads input description of known IANA timezone abbreviations (including single-
letter military zone names, e.g. Zulu 'Z'), and list of zones from Olson/tzcode
main.zi file, and then generates set of C/C++ macro calls defining timezone
with all attributes.

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
future. To simplify future updates we leave some holes in list. When the
generated header already exists, the ids assigned there are reused as is, and
newly appeared zones get ids from the spare range, so the ids stay stable
across timezone database updates.

Another artifact - is Lua config file, which defines table for converting of
timezone index to the timezone name. e.g.
    [25] = "Z",
    [1000] = "Etc/GMT",

This config file is loaded by datetime.lua module into public `TZ` table.

NB! In a future, for the next versions of Olson/tzdata database, we plan to
automate updates of generated list. At the moment we leave it as manual process.

=cut

use strict;
use warnings;

my $zone_abbrevs_file = shift || 'zone-abbrevs.txt';
my $tzdata_zi_file    = shift || 'main.zi';
my $output_h_file     = shift || 'timezones.h';
my $output_lua_file   = shift || 'timezones.lua';

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
        TZ_OLSON     => 0x20,
        TZ_ALIAS     => 0x40,
        TZ_DST       => 0x80,
    );
    require constant; constant->import(\%flags);
    %FlagName = reverse %flags;
}

# ACT    -05:00          # Acre Time (South America)
# ACT    +10:30 / +09:30 # Australian Central Time (Australia)
# ACWST  +08:45          # Australian Central Western Standard Time (Australia)

my $EntryRx = do {
    my $name   = '[A-Z][A-Za-z]{0,5}';
    my $offset = '[+-] (?: [01][0-9] | [2][0-3]) [:] [0-5][0-9]';
    qr< ($name) \s+ ($offset) (?: \s+ / \s+ ($offset))? \s+ #>xo
};

my %ZoneAbbrevs;
my %ZoneAbbrevNames;

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

        if (/(Daylight|Summer)/) {
            $flags |= TZ_DST;
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
        $ZoneAbbrevNames{$name}++;
    }
}

my %ZoneNames;
my %ZoneIds;    # number to name
my %ZoneRevIds; # name to number
my %Aliases;    # name to name

my $stable_ids = 0;
my %OldIds;     # name to id, ids assigned in the existing generated file
my %OldZones;   # zone name to id, zones of the existing generated file
my %OldAbbrevs; # abbreviation name to [id, offset, flags]
my %OldAliases; # alias name to [id, target zone name]
my %UsedIds;    # id to 1, already assigned ids
my $next_free_id = 0;
my %AbbrevOut;  # id to [offset, name, flags]
my %UniqueOut;  # id to name
my %AliasOut;   # alias name to [id, target zone name]
my %UniqueById; # id to name, unique zones only

sub read_zi_file($) {
    my $filename = shift;
    open( my $fh, '<:raw', $filename )
      or die qq<Could not open '$filename' for reading: '$!'>;


    while (<$fh>) {
        next if /^#/;
        if (/^Zone\s+([A-Za-z\-\_\/]+)\s+/) {
            my $zone = $1;
            next unless $zone =~ q|/|;
            $ZoneNames{$zone}++;
        } elsif (/^[#]?Link\s+(\S+)\s+(\S+)/) {
            my $zone = $1;
            my $link = $2;
            # do not create alias if it's known abbreviation
            next if defined $ZoneAbbrevNames{$link};
            $Aliases{$link} = $zone;
        }
    }

}

my $out_h;
my $out_lua;

# Timezone ids must not exceed MAX_TZINDEX in src/lib/core/datetime.h.
my $max_tzindex = 1024;

# Read the existing generated file, if any, and reuse the ids assigned there.
# It keeps the ids stable across timezone database updates, which is required
# to not break datetime values already stored on disk, as their timezone index
# is a part of the MessagePack format (see mp_datetime.c).
sub read_existing_file() {
    open( my $fh, '<:raw', $output_h_file ) or return;

    while (<$fh>) {
        my $id;
        if ( my ( $a_id, $offset, $a_name, $flags ) =
             /\bZONE_ABBREV\(\s*(\d+),\s*(-?\d+),\s*"([^"]+)",\s*(.*)\)\s*$/x )
        {
            $id = $a_id;
            $OldAbbrevs{$a_name} = [ $id, $offset, $flags ];
            $OldIds{$a_name}     = $id;
        }
        elsif ( my ( $z_id, $z_name ) =
                /\bZONE_UNIQUE\(\s*(\d+),\s*"([^"]+)"\s*\)\s*$/x )
        {
            $id = $z_id;
            $OldZones{$z_name} = $id;
            $OldIds{$z_name}   = $id;
        }
        elsif ( my ( $l_id, $l_alias, $l_target ) =
                /\bZONE_ALIAS\(\s*(\d+),\s*"([^"]+)",\s*"([^"]+)"\s*\)\s*$/x )
        {
            $id = $l_id;
            $OldAliases{$l_alias} = [ $id, $l_target ];
            $OldIds{$l_alias}     = $id;
        }
        else {
            next;
        }
        $UsedIds{$id} = 1;
    }
    close($fh);
    return unless %UsedIds;
    $stable_ids   = 1;
    $next_free_id = ( sort { $b <=> $a } keys %UsedIds )[0] + 1;
}

sub assign_new_id() {
    while ( exists $UsedIds{$next_free_id} ) {
        $next_free_id++;
    }
    die "too many timezone ids, raise MAX_TZINDEX in datetime.h"
        if $next_free_id >= $max_tzindex;
    $UsedIds{$next_free_id} = 1;
    return $next_free_id;
}

# Return an id for the zone name, reusing the previously assigned one when the
# generated file already exists, otherwise assign a new id from the spare range.
sub assign_id {
    my ( $name ) = @_;
    return $OldIds{$name} if $stable_ids and exists $OldIds{$name};
    return assign_new_id() if $stable_ids;
    return next_id($name);
}

sub open_out_files() {
    open($out_h, ">$output_h_file") or
        die qq<Could not open '$output_h_file' for writing: '$!'>;
    open($out_lua, ">$output_lua_file") or
        die qq<Could not open '$output_lua_file' for writing: '$!'>;
}

# first we generate abbreviations list
sub gen_abbrevs() {
    my %abbrev;  # name -> [offset, flags]

    foreach my $encoded ( sort { $a <=> $b } keys %ZoneAbbrevs ) {
        my ( $flags, $offset, $name ) = @{ $ZoneAbbrevs{$encoded} };
        $abbrev{$name} = [ $offset, $flags ];
    }
    # keep abbreviations of the existing file
    foreach my $name ( sort keys %OldAbbrevs ) {
        next if exists $abbrev{$name};
        $abbrev{$name} = [ $OldAbbrevs{$name}[1], $OldAbbrevs{$name}[2] ];
    }
    foreach my $name ( sort keys %abbrev ) {
        my ( $offset, $flags ) = @{ $abbrev{$name} };
        my $id = assign_id($name);
        $ZoneIds{$id} = $name;

        if ( $flags != 0 ) {
            my @names;

            while ( ( my $flag = $flags & -$flags ) != 0 ) {
                push @names, $FlagName{$flag} || die sprintf '%4d', $flag;
                $flags &= ~$flag;
            }

            $flags = join '|', @names;
        }

        $AbbrevOut{$id} = [ $offset, $name, $flags ];
    }
}

# second we enumerate all known from main.zi primary zone names
sub gen_primary_zones() {
    my %zone;    # name -> 1
    $zone{$_} = 1 for keys %ZoneNames;
    # keep zones of the existing file, they may become links or disappear
    $zone{$_} = 1 for keys %OldZones;
    foreach my $zonename ( sort keys %zone ) {
        my $id = assign_id($zonename);
        while ( exists $UniqueById{$id} ) {
            warn "reassigning id $id for $zonename\n";
            $id = assign_new_id();
        }
        $UniqueById{$id}       = $zonename;
        $UniqueOut{$id}        = $zonename;
        $ZoneRevIds{$zonename} = $id;
        $ZoneIds{$id}          = $zonename;
    }
}

# third we enumerate all aliases for primary zones
sub gen_aliases() {
    my %alias;   # alias name -> target zone name

    foreach my $alias ( sort keys %Aliases ) {
        next if defined $ZoneAbbrevNames{$alias};
        next if exists $ZoneRevIds{$alias};
        $alias{$alias} = $Aliases{$alias};
    }
    # keep aliases of the existing file
    foreach my $alias ( sort keys %OldAliases ) {
        next if exists $alias{$alias};
        next if exists $ZoneRevIds{$alias};
        $alias{$alias} = $OldAliases{$alias}[1];
    }
    foreach my $alias ( sort keys %alias ) {
        my $target = $alias{$alias};
        my $id;
        if ( exists $OldIds{$alias} ) {
            $id = $OldIds{$alias};
        }
        elsif ( $stable_ids and exists $ZoneRevIds{$target} ) {
            $id = $ZoneRevIds{$target};
        }
        elsif ( $stable_ids ) {
            $id = assign_new_id();
        }
        else {
            $id = $ZoneRevIds{$target}
                || die qq/Could not find id for alias target '$target'/;
        }
        $AliasOut{$alias} = [ $id, $target ];
    }
}

sub gen_c_header() {
    printf $out_h "/* Automatically generated by gen-zone-abbrevs.pl */\n";

    gen_abbrevs();
    gen_primary_zones();
    gen_aliases();

    foreach my $id ( sort { $a <=> $b } keys %AbbrevOut ) {
        my ( $offset, $name, $flags ) = @{ $AbbrevOut{$id} };
        printf $out_h "ZONE_ABBREV(%4d, %4d, \"%s\", %s)\n",
          $id, $offset, $name, $flags;
    }
    foreach my $id ( sort { $a <=> $b } keys %UniqueOut ) {
        printf $out_h "ZONE_UNIQUE(%4d, \"%s\")\n", $id, $UniqueOut{$id};
    }
    foreach my $alias ( sort keys %AliasOut ) {
        my ( $id, $target ) = @{ $AliasOut{$alias} };
        printf $out_h "ZONE_ALIAS(%4d, \"%s\", \"%s\")\n", $id, $alias, $target;
    }
}

sub gen_lua_config() {
    print $out_lua "-- Automatically generated by gen-zone-abbrevs.pl\n";
    print $out_lua "return {\n";
    foreach my $nextid (sort { $a <=> $b } keys %ZoneIds) {
        printf $out_lua "    [%4d] = \"%s\",\n", $nextid, $ZoneIds{$nextid};
        printf $out_lua "    [\"%s\"] = %d,\n", $ZoneIds{$nextid}, $nextid;
    }
    print $out_lua "}\n";
}

read_abbrevs_file($zone_abbrevs_file);
read_zi_file($tzdata_zi_file);
read_existing_file();
open_out_files();
gen_c_header();
gen_lua_config()
