#!/usr/bin/perl

# Tarantool/Box config below

use strict;
use warnings;
BEGIN {
    sub mPOP::Config::GetValue ($) {
        die;
    }
}
use FindBin qw($Bin);
use lib "$Bin";
use Carp qw/confess/;

use Test::More tests => 24;
use Test::Exception;

use List::MoreUtils qw/zip/;

use MR::Pending;

local $SIG{__DIE__} = \&confess;

our $CLASS;
BEGIN { $CLASS = $ENV{BOXCLASS} || 'MR::Tarantool::Box'; eval "require $CLASS" or die $@; }

use constant ILL_PARAM         => qr/Error 00000202/;
use constant TUPLE_NOT_EXISTS  => qr/Error 00003102/;
use constant TUPLE_EXISTS      => qr/Error 00003702/;
use constant INDEX_VIOLATION   => qr/Error 00003802/;

use constant NO_SUCCESS        => qr/no success after/;

use constant TOO_BIG_FIELD => qr/too big field/;

our $box;
our $server = (shift || $ENV{BOX}) or die;
our %opts = (
    debug => $ENV{DEBUG}||0,
    ipdebug => $ENV{IPDEBUG}||0,
    raise => 1,
);

sub cleanup ($) {
    my ($id) = @_;
    die unless defined $id;
    ok defined $box->Delete($id), 'delete of possible existing record';
    ok $box->Delete($id) == 0, 'delete of non existing record';
}

sub def_param  {
    my $format = shift || 'l& SSLL';
    return { servers => $server,
             name    => $CLASS,
             namespaces => [ {
                 indexes => [ {
                     index_name   => 'primary_id',
                     keys         => [0],
                 }, {
                     index_name   => 'primary_email',
                     keys         => [1],
                 }, ],
                 namespace     => 0,
                 format        => $format,
                 default_index => 'primary_id',
                 name          => 'main',
             } ],
             default_space => "main",
             %opts,
         }
}

$box = $CLASS->new(def_param('l&SSLL&'));
ok $box->isa($CLASS), 'connect';

my $box2 = $CLASS->new(def_param('l&SSLL&'));
ok $box2->isa($CLASS), 'connect';

my $tt =     [ [1, 'rtokarev@corp.mail.ru',     11, 111, 1111, 11111, "1111111111111"],
               [2, 'vostrikov@corp.mail.ru',    22, 222, 2222, 22222, "2222222222222"],
               [3, 'aleinikov@corp.mail.ru',    33, 333, 3333, 33333, "3333333333333"],
               [4, 'roman.s.tokarev@gmail.com', 44, 444, 4444, 44444, "4444444444444"],
               [5, 'vostrIIIkov@corp.mail.ru',  55, 555, 5555, 55555, "5555555555555"] ];

foreach my $tuple (@$tt) {
    ok eval{ $box->Delete($tuple->[0]); 1 }, "delete$tuple->[0]";
}

foreach my $tuple (@$tt) {
    ok $box->Insert(@$tuple), "insert$tuple->[0]";
}

my @box = ($box, $box2);
my @select = ([1,2,3],[2,3,4]);

my $onok = sub {
    my ($i, $data) = @_;
    ok $data, "select $i data";
    ok @$data, "select $i \@data";
    ok $data->[0], "select $i \$data->[0]";
    is_deeply $data->[0], [ @$tt[map $_-1, @{$select[$i]}] ], "select $i tuples";
};

my $onerror = sub {
    my ($i, $err) = @_;
    ok !1, "select $i failed";
};

my $ontry = sub {
    my ($i) = @_;
    return $box[$i]->Select($select[$i], {want => "arrayref", return_fh => 1});
};

MR::Pending->new(
    name     => "PENDINGTEST",
    maxtime  => 1.1,
    itertime => 0.01,
    pending  => [map { MR::Pending::Item->new(
        id           => $_,
        onok         => $onok,
        onerror      => $onerror,
        onretry      => $ontry,
        timeout      => 0.5,
        retry_delay  => 0.001,
        retry        => 3,
    ) } 0],
)->work;

MR::Pending->new(
    name     => "PENDINGTEST",
    maxtime  => 1.1,
    itertime => 0.01,
    pending  => [map { MR::Pending::Item->new(
        id           => $_,
        onok         => $onok,
        onerror      => $onerror,
        onretry      => $ontry,
        timeout      => 0.5,
        retry_delay  => 0.001,
        retry        => 3,
    ) } (0, 1)],
)->work;




__END__

space[0].enabled = 1
space[0].index[0].type = "HASH"
space[0].index[0].unique = 1
space[0].index[0].key_field[0].fieldno = 0
space[0].index[0].key_field[0].type = "NUM"
space[0].index[1].type = "HASH"
space[0].index[1].unique = 1
space[0].index[1].key_field[0].fieldno = 1
space[0].index[1].key_field[0].type = "STR"

space[20].enabled = 1
space[20].index[0].type = "HASH"
space[20].index[0].unique = 1
space[20].index[0].key_field[0].fieldno = 0
space[20].index[0].key_field[0].type = "NUM64"


space[26].enabled = 1
space[26].index[0].type = "HASH"
space[26].index[0].unique = 1
space[26].index[0].key_field[0].fieldno = 0
space[26].index[0].key_field[0].type = "NUM"
space[26].index[1].type = "TREE"
space[26].index[1].unique = 0
space[26].index[1].key_field[0].fieldno = 1
space[26].index[1].key_field[0].type = "STR"
space[26].index[2].type = "TREE"
space[26].index[2].unique = 0
space[26].index[2].key_field[0].fieldno = 1
space[26].index[2].key_field[0].type = "STR"
space[26].index[2].key_field[1].fieldno = 2
space[26].index[2].key_field[1].type = "NUM"



space[27].enabled = 1
space[27].index[0].type = "HASH"
space[27].index[0].unique = 1
space[27].index[0].key_field[0].fieldno = 0
space[27].index[0].key_field[0].type = "NUM"
space[27].index[1].type = "HASH"
space[27].index[1].unique = 1
space[27].index[1].key_field[0].fieldno = 1
space[27].index[1].key_field[0].type = "STR"

space[27].index[2].type = "TREE"
space[27].index[2].unique = 1
space[27].index[2].key_field[0].fieldno = 2
space[27].index[2].key_field[0].type = "STR"

space[27].index[2].type = "TREE"
space[27].index[2].unique = 1
space[27].index[2].key_field[0].fieldno = 3
space[27].index[2].key_field[0].type = "STR"

space[27].index[3].type = "TREE"
space[27].index[3].unique = 1
space[27].index[3].key_field[0].fieldno = 2
space[27].index[3].key_field[0].type = "STR"
space[27].index[3].key_field[1].fieldno = 3
space[27].index[3].key_field[1].type = "STR"
