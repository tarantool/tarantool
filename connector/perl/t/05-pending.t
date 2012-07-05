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
use lib "$Bin/../lib";

use Carp qw/confess/;

use Test::More tests => 61;
use Test::Exception;

use List::MoreUtils qw/zip/;

BEGIN {
    use_ok 'Test::Tarantool';
    use_ok 'MR::Pending';
    use_ok 'Time::HiRes', 'time';
}

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


our ($server1, $server2); # = shift || $ENV{BOX};

my $tarantool_config = "$Bin/data/pending.t.cfg";
ok -r $tarantool_config, "-r $tarantool_config";
my $tnt_dir = "$Bin/data";
ok -d $tnt_dir, "-d $tnt_dir";
my @tnt_srv;

for ($server1, $server2) {
    SKIP: {
        skip 'The test uses external tarantool', 2 if $_;

        push @tnt_srv => Test::Tarantool->run(
            cfg => $tarantool_config,
            script_dir => $tnt_dir
        );
        ok $tnt_srv[-1], 'server instance created';
        diag $tnt_srv[-1]->log unless ok $tnt_srv[-1]->started,
            'server is started';

        $_ = sprintf '127.0.0.1:%d', $tnt_srv[-1]->primary_port;
    }
}


sub server_id($) {
    my $box = shift;
    $box->Call(tst_pending_server_name => [], { unpack_format => '$' })->[0][0];
}

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
    my $server = shift || $server1;
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

$box = $CLASS->new(def_param('l&SSLL&', $server1));
ok $box->isa($CLASS), 'connect';

my $box2 = $CLASS->new(def_param('l&SSLL&', $server2));
ok $box2->isa($CLASS), 'connect';


ok $box->Insert(151274, 'box1', 1, 2, 3, 4, 5), 'insert the first id';
ok $box2->Insert(151274, 'box2', 1, 2, 3, 4, 5), 'insert the second id';



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


is_deeply eval {
    $box->Call('tst_lua_test' => [], { unpack_format => '$' })
}, [['test']], 'lua functions are working';


# note explain
# $box->Select(1, { return_fh => 1 });

my $cdelayed = $box->Call(
    'tst_lua_test' => [], { unpack_format => '$', return_fh => 1 }
);

isa_ok $cdelayed, 'HASH';

is_deeply $cdelayed->{continue}->(), [['test']],
    'lua functions are working with return_fh';


is server_id($box), 'box1', 'server1 id';
is server_id($box2), 'box2', 'server2 id';


my @box = ($box, $box2);

my $onok = sub {
    my ($i, $data) = @_;
    is_deeply $data, [[[ 'box' . ($i + 1) ]]], "selected server@{[ $i + 1]} id";
    1;
};

my $onerror = sub {
    my ($i, $err) = @_;
    fail "select $i failed";
};

my $ontry = sub {
    my ($i) = @_;
    return $box[$i]->Call(
        tst_pending_server_name => [],
        {
            return_fh       => 1,
            unpack_format   => '$'
        }
    );
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
    ) } (0, 1)],
)->work;



my $started = time;
my @done;

$ontry = sub {
    my ($i, $pend) = @_;
    $i = 0 if $pend->is_second;
    return $box[$i]->Call(
        tst_pending_server_pause => [],
        {
            return_fh       => 1,
            unpack_format   => '$'
        }
    );
};

$onok = sub {
    my ($i, $data) = @_;
    $done[$i] = time - $started;
    is_deeply $data, [[[ 'box' . ($i + 1) ]]], "selected server@{[ $i + 1]} id";
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
        timeout      => 0.8,
        retry_delay  => 0.001,
        retry        => 3,
    ) } (0, 1)],
)->work;

cmp_ok $done[0], '<', .15, 'first server response time less than .15 seconds';
cmp_ok $done[0], '>', .1, 'first server response time more than .1 seconds';

cmp_ok $done[1], '<', 1.1, 'first server response time less than 1.1 seconds';
cmp_ok $done[1], '>', 1, 'first server response time more than 1 seconds';




note '** check onsecondary_retry';

for ( 1 .. 2) {
    my @res = ([], []);
    $started = time;

    {
        MR::Pending->new(
            name                => "PENDINGTEST",
            maxtime             => 1.1,
            itertime            => 0.01,
            pending  => [
                MR::Pending::Item->new(
                    id           => 0,
                    onok         => sub {
                        like $_[0], qr{^\d+$},
                            "first request is done id: $_[0]";
                        push @{ $res[ $_[0] ] } => {
                            box     => $_[1][0][0][0],
                            time    => time - $started
                        };
                        return 1;
                    },
                    onerror      => $onerror,
                    onretry      => $ontry,
                    timeout      => .9,
                    retry_delay  => 0.4,
                    retry        => 3,
                ),
                MR::Pending::Item->new(
                    id                  => 1,
                    onok                => sub {
                        like $_[0], qr{^\d+$},
                            "second request is done id: $_[0]";
                        push @{ $res[ $_[0] ] } => {
                            box     => $_[1][0][0][0],
                            time    => time - $started
                        };
                        return 1;
                    },
                    onerror             => $onerror,
                    onretry             => $ontry,
                    timeout             => 0.9,
                    second_retry_delay  => 0.4,
                    retry_delay         => 0.4,
                    retry               => 3,
                ),
            ]
        )->work;

    }

    is scalar @{ $res[0] }, 1, 'first callback was touched once';
    is scalar @{ $res[1] }, 1, 'second callback was touched once';
    is $res[0][0]{box}, $res[1][0]{box}, 'both requests were done by one box';
    is $res[0][0]{box}, 'box1', 'it was a box1';
}


my $onsecondary_retry_touched = 0;
$ontry = sub {
    my ($i, $pend) = @_;
    $i = 0 if $pend->is_second;
    $onsecondary_retry_touched++ if $pend->is_second;
    return $box[$i]->Call(
        tst_pending_server_pause => [ $pend->is_second ? 3 : 1 ],
        {
            return_fh       => 1,
            unpack_format   => '$'
        }
    );
};


$started = time;
$box->Call(tst_pending_server_pause => [ .3 ], { unpack_format => '$' });
cmp_ok time - $started, '>=', .3, 'tst_pending_server_pause(.3)';
cmp_ok time - $started, '<', .4, 'tst_pending_server_pause(.3)';

note "** another test";
$started = time;
my @res = ([], []);
{
    my $pd = MR::Pending->new(
        name                => "PENDINGTEST",
        maxtime             => 1.5,
        itertime            => 0.01,
        pending  => [
            MR::Pending::Item->new(
                id           => 0,
                onok         => sub {
                    like $_[0], qr{^\d+$},
                        "first request is done id: $_[0]";
                    push @{ $res[ $_[0] ] } => {
                        box     => $_[1][0][0][0],
                        time    => time - $started
                    };
                    return 1;
                },
                onerror      => $onerror,
                onretry      => $ontry,
                timeout      => 3,
                retry_delay  => 0.4,
                retry        => 3,
            ),
            MR::Pending::Item->new(
                id                  => 1,
                onok                => sub {
                    like $_[0], qr{^\d+$},
                        "second request is done id: $_[0]";
                    push @{ $res[ $_[0] ] } => {
                        box     => $_[1][0][0][0],
                        time    => time - $started
                    };
                    return 1;
                },
                onerror             => $onerror,
                onretry             => $ontry,
                timeout             => 3,
                second_retry_delay  => 0.1,
                retry_delay         => 0.4,
                retry               => 3,
            ),
        ]
    );
    $pd->work;
    is scalar @{ $res[0] }, 1, 'first callback was touched once';
    is scalar @{ $res[1] }, 1, 'second callback was touched once';
    is $res[0][0]{box}, 'box1', 'first replay was from box1';
    cmp_ok $res[0][0]{time}, '>=', 1, 'first replay took more than 1 second';
    cmp_ok $res[1][0]{time}, '>=', 1, 'second replay took more than 1 second';
    is $res[1][0]{box}, 'box2', 'second replay was from box2';
    cmp_ok time - $started, '<', 1.1, 'Both requests got less than 1.1 second';
    is $onsecondary_retry_touched, 1, 'onsecondary_retry touched once';

}

cmp_ok time - $started, '>', 3, 'Destructor waited for longer request';

