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

use Test::More tests => 360;
use Test::Exception;

use List::MoreUtils qw/zip/;

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
cleanup 13;

ok $box->Insert(13, q/some_email@test.mail.ru/, 1, 2, 3, 4, '777',{space => 'main'}), 'insert';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '777'], 'select/insert';

ok $box->Insert(13, q/some_email@test.mail.ru/, 2, 2, 3, 4, '666',{namespace => 'main'}), 'replace';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 2, 3, 4, '666'], 'select/replace';

ok $box->Update(13, 3 => 1) == 1, 'update of some field';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, '666'], 'select/update';

ok $box->Append( 13, 6 => 'APPEND') , 'append op';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, '666APPEND'], 'select/append';

ok $box->Prepend(13, 6 => 'PREPEND'), 'prepend op';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, 'PREPEND666APPEND'], 'select/prepend';

ok $box->Cutbeg(13, 6 => 2), 'cutbeg op';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, 'EPEND666APPEND'], 'select/cutbeg';

ok $box->Cutend(13, 6 => 2), 'cutend op';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, 'EPEND666APPE'], 'select/cutend';

ok $box->Substr(13, 6 => [3,4,'12345']), 'substr op';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, 'EPE123456APPE'], 'select/substr';

ok $box->UpdateMulti(13, [6 => splice => [0]]), 'generic splice (offset = 0)';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4, ''], 'select/splice (offset = 0)';

cleanup 13;

ok $box->Insert(13, q/some_email@test.mail.ru/, 1, 2, 3, 4, '123456789'), 'insert';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/insert';

throws_ok sub { $box->UpdateMulti(13, [6 => splice => [-10]]) }, ILL_PARAM, "splice/bad_params_1";

ok $box->UpdateMulti(13, [6 => splice => [100]]), "splice/big_offset";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select';

ok $box->UpdateMulti(13, [6 => splice => [5]]), "splice/cut_tail_pos_offset";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '12345'], 'select';

ok $box->UpdateMulti(13, [6 => splice => [-2]]), "splice/cut_tail_neg_offset";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123'], 'select';

ok $box->Insert(13, q/some_email@test.mail.ru/, 1, 2, 3, 4, '123456789'), 'replace';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select';

ok $box->UpdateMulti(13, [6 => splice => [8, 1000]]), "splice/big_pos_length";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '12345678'], 'select';

ok $box->UpdateMulti(13, [6 => splice => [1, -1000]]), "splice/big_neg_length";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '12345678'], 'select';

ok $box->Insert(13, q/some_email@test.mail.ru/, 1, 2, 3, 4, '123456789'), 'replace';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select';

ok $box->UpdateMulti(13, [6 => splice => [0x7fffffff]]), "splice/max_offset";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select';

ok $box->UpdateMulti(13, [6 => splice => [1, 2]], [6 => splice => [-2, -1, 'qwe']]), "splice/multi";
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '14567qwe9'], 'select';



cleanup 13;
cleanup 14;
throws_ok sub { $box->Replace(13, q/some_email@test.mail.ru/, 5, 5, 5, 5, '555555555')}, TUPLE_NOT_EXISTS, 'replace';

ok $box->Add(13, q/some_email@test.mail.ru/, 1, 2, 3, 4, '123456789'), 'add';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/add';

throws_ok sub { $box->Add(13, q/some_email@test.mail.ru/, 5, 5, 5, 5, '555555555')}, TUPLE_EXISTS, 'add2';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/add2';
is_deeply scalar $box->Select(q/some_email@test.mail.ru/, {use_index => "primary_email"}), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/add2';

throws_ok sub { $box->Add(14, q/some_email@test.mail.ru/, 5, 5, 5, 5, '555555555')}, INDEX_VIOLATION, 'add3';

ok $box->Add(14, q/some1email@test.mail.ru/, 1, 2, 3, 4, '123456789'), 'add4';
is_deeply scalar $box->Select(14), [14, 'some1email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/add4';
is_deeply scalar $box->Select(q/some1email@test.mail.ru/, {use_index => "primary_email"}), [14, 'some1email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/add4';

throws_ok sub { $box->Replace(13, q/some1email@test.mail.ru/, 6, 6, 6, 6, '666666666')}, INDEX_VIOLATION, 'replace';
throws_ok sub { $box->Set(13, q/some1email@test.mail.ru/, 6, 6, 6, 6, '666666666')}, INDEX_VIOLATION, 'set';

ok $box->Set(13, q/some_email@test.mail.ru/, 5, 5, 5, 5, '555555555'), 'set';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 5, 5, 5, 5, '555555555'], 'select/set';

ok $box->Replace(13, q/some_email@test.mail.ru/, 1, 2, 3, 4, '123456789'), 'replace';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 'select/replace';


is_deeply [$box->Select([13], {raise => 0, hash_by => 0, raw => 1})], [{13 => [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789']}], 'select/rawhash1';
is_deeply [$box->Select([13,14], {raise => 0, hash_by => 0, raw => 1})], [{13 => [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], 14 => [14, 'some1email@test.mail.ru', 1, 2, 3, 4, '123456789']}], 'select/rawhash2';


do {
    my $continuation = $box->Select(13,{ return_fh => 1 });
    ok $continuation, "select/continuation";

    my $rin = '';
    vec($rin,$continuation->{fh}->fileno,1) = 1;
    my $ein = $rin;
    ok 0 <= select($rin,undef,$ein,2), "select/continuation/select";

    my $res = $continuation->{continue}->();
    use Data::Dumper;
    is_deeply $res, [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], "select/continuation/result";
};

our $ANYEVENT = 1 && eval { require AnyEvent; 1 };
SKIP:{
    skip "AnyEvent not found", 60 unless $ANYEVENT;

    local $opts{raise} = 0;
    $box = $CLASS->new(def_param('l&SSLL&'));

    my $tt =     [ [1, 'rtokarev@corp.mail.ru',     11, 111, 1111, 11111, "1111111111111"],
                   [2, 'vostrikov@corp.mail.ru',    22, 222, 2222, 22222, "2222222222222"],
                   [3, 'aleinikov@corp.mail.ru',    33, 333, 3333, 33333, "3333333333333"],
                   [4, 'roman.s.tokarev@gmail.com', 44, 444, 4444, 44444, "4444444444444"],
                   [5, 'vostrIIIkov@corp.mail.ru',  55, 555, 5555, 55555, "5555555555555"] ];

    foreach my $tuple (@$tt) {
        cleanup $tuple->[0];
    }

    AnyEvent->now_update;
    my $cv = AnyEvent->condvar;
    foreach my $tuple (@$tt) {
        $cv->begin;
        ok $box->Insert(@$tuple, {callback => sub { ok $_[0], "async/insert$tuple->[0]/result"; $cv->end; }}), "async/insert$tuple->[0]";
    }
    $cv->recv;


    AnyEvent->now_update;
    $cv = AnyEvent->condvar;
    $cv->begin;
    ok $box->Select(1,2,3,{callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  is_deeply $res, [@$tt[0,1,2]], "async/select1/result";
                           }}), "async/select1";

    $cv->begin;
    ok $box->Select(4,5,{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  is_deeply $res, [@$tt[3,4]], "async/select2/result";
                           }}), "async/select2";

    $cv->recv;


    AnyEvent->now_update;
    $cv = AnyEvent->condvar;
    foreach my $tuple (@$tt) {
        $tuple->[4] += 10000;
        $cv->begin;
        ok $box->UpdateMulti($tuple->[0], [ 4 => add => 10000 ], {callback => sub { ok $_[0], "async/update1-$tuple->[0]/result"; $cv->end; }}), "async/update1-$tuple->[0]";
    }
    $cv->begin;
    ok $box->Select((map{$_->[0]}@$tt),{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  is_deeply $res, $tt, "async/update1-select/result";
                           }}), "async/update1-select";
    $cv->recv;

    AnyEvent->now_update;
    $cv = AnyEvent->condvar;
    foreach my $tuple (@$tt) {
        $tuple->[4] += 10000;
        $cv->begin;
        ok $box->UpdateMulti($tuple->[0], [ 4 => add => 10000 ], {want_result => 1, callback => sub { is_deeply $_[0], $tuple, "async/update2-$tuple->[0]/result"; $cv->end; }}), "async/update2-$tuple->[0]";
    }
    $cv->begin;
    ok $box->Select((map{$_->[0]}@$tt),{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  is_deeply $res, $tt, "async/update2-select/result";
                           }}), "async/update2-select";
    $cv->recv;

    AnyEvent->now_update;
    $cv = AnyEvent->condvar;
    foreach my $tuple (@$tt) {
        $cv->begin;
        ok $box->Delete($tuple->[0], {want_result => 1, callback => sub { is_deeply $_[0], $tuple, "async/delete-$tuple->[0]/result"; $cv->end; }}), "async/delete-$tuple->[0]";
    }
    $cv->begin;
    ok $box->Select((map{$_->[0]}@$tt),{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  is_deeply $res, [], "async/delete-select/result";
                           }}), "async/delete-select";
    $cv->recv;
}

sub countwarn {
    my ($qr, $counter) = @_;
    return sub {
        ++$$counter if $_[0] =~ $qr;
        warn @_;
    };
};

do {
    local $server = "127.0.0.1:1111";
    local $opts{raise} = 0;
    my $try = 3;

    my $counter = 0;
    local $SIG{__WARN__} = countwarn(qr/refused/i, \$counter);

    my $box = $CLASS->new(def_param('l&SSLL&'));

    throws_ok sub{my$x=$box->Select(13,{ want => "arrayref", raise => 1 })}, NO_SUCCESS, "reject/select/raise/sync";
    ok $counter == $try, "reject/select/raise/sync/counter";
    $counter = 0;

    ok !$box->Select(13,{ want => "arrayref", raise => 0 }), "reject/select/noraise/sync";
    ok $counter == $try, "reject/select/noraise/sync/counter";
    $counter = 0;

    my $continuation = $box->Select(13,{ return_fh => 1, raise => 0 });
    ok !$continuation, "reject/select/continuation";
    ok $counter == 1, "reject/select/continuation/counter";
    $counter = 0;


  SKIP:{
        skip "AnyEvent not found", 5 unless $ANYEVENT;

        AnyEvent->now_update;
        my $cv = AnyEvent->condvar;
        $cv->begin;
        ok $box->Select(4,5,{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  ok !$res, "reject/select/async/noraise/cb";
                                  ok $box->Error, "reject/select/async/noraise/cb/error";
                                  ok $box->ErrorStr, "reject/select/async/noraise/cb/errorstr";
                              }}), "reject/select/async/noraise";

        $cv->recv;
        ok $counter == $try, "reject/select/async/noraise/counter";
        $counter = 0;
    }
};

do {
    my $pid;
    local $SIG{INT} = $SIG{TERM} = sub { kill 'TERM', $pid };

    $pid = fork();
    die unless defined $pid;
    unless($pid) {
        $0 = "$0 <SERVER>";
        my $stop = 0;
        my $h;
        my $l = IO::Socket::INET->new(
            LocalAddr => '127.0.0.1',
            LocalPort => 1111,
            Proto => 'tcp',
            Listen => 10,
            Blocking => 1,
            ReuseAddr => 1,
        ) or die $!;
        $SIG{INT} = $SIG{TERM} = sub { ++$stop; close $l; close $h; exit; };
        while(!$stop) {
            $h = $l->accept;
            my $data;
            while($h->read($data,1024) > 0) { 0; }
            close $h;
        }
        exit;
    }


    local $server = "127.0.0.1:1111";
    local $opts{raise} = 0;
    local $opts{timeout} = 0.1;
    local $opts{select_timeout} = 0.1;

    my $try = 3;

    my $counter = 0;
    local $SIG{__WARN__} = countwarn(qr/timed? ?out/i, \$counter);

    my $box = $CLASS->new(def_param('l&SSLL&'));

    sleep 1;

    throws_ok sub{my$x=$box->Select(13,{ want => "arrayref", raise => 1 })}, NO_SUCCESS, "timeout1/select/raise/sync";
    ok $counter == $try, "timeout1/select/raise/sync/counter";
    $counter = 0;

    ok !$box->Select(13,{ want => "arrayref", raise => 0 }), "timeout1/select/noraise/sync";
    ok $counter == $try, "/counter";
    $counter = 0;

    my $continuation = $box->Select(13,{ return_fh => 1, raise => 0 });
    ok $continuation, "timeout1/select/continuation";
    ok !$continuation->{continue}->(), "timeout1/select/continuation/result";
    ok $counter == 1, "timeout1/select/continuation/counter";
    $counter = 0;


  SKIP:{
        skip "AnyEvent not found", 5 unless $ANYEVENT;

        AnyEvent->now_update;
        my $cv = AnyEvent->condvar;
        $cv->begin;
        ok $box->Select(4,5,{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  ok !$res, "timeout1/select/async/noraise/cb";
                                  ok $box->Error, "timeout1/select/async/noraise/cb/error";
                                  ok $box->ErrorStr, "timeout1/select/async/noraise/cb/errorstr";
                              }}), "timeout1/select/async/noraise";

        $cv->recv;
        ok $counter == $try, "timeout1/select/async/noraise/counter";
        $counter = 0;
    }

    kill 'TERM', $pid;
};

do {
    my $pid;
    local $SIG{INT} = $SIG{TERM} = sub { kill 'TERM', $pid };

    $pid = fork();
    die unless defined $pid;
    unless($pid) {
        $0 = "$0 <SERVER>";
        my $stop = 0;
        my $h;
        my @ok = (0,0,1,0,0,1,1,0,0,1);
        my $l = IO::Socket::INET->new(
            LocalAddr => '127.0.0.1',
            LocalPort => 1111,
            Proto => 'tcp',
            Listen => 10,
            Blocking => 1,
            ReuseAddr => 1,
        ) or die $!;
        my ($host, $port) = split /:/, $server;
        my $box = IO::Socket::INET->new(
            PeerAddr => $host,
            PeerPort => $port,
            Proto => 'tcp',
            Blocking => 1,
        ) or die;
        $SIG{INT} = $SIG{TERM} = sub { ++$stop; close $l; close $h; close $box; exit; };

        while(!$stop) {
            $h = $l->accept;
            $h->blocking(1);
            my $data = '';
            if (shift @ok) {
                while(!$stop) {
                    $h->blocking(0);
                    $h->read($data,1024,length$data);
                    if(length$data) {
                        $h->blocking(1);
                        $h->read($data,12-length$data,length$data) while length $data < 12;
                        my ($len) = unpack 'x4L', $data;
                        $h->read($data,12+$len-length$data,length$data) while length $data < 12+$len;
                        $box->write($data);

                        $data = '';
                        $box->read($data,12-length$data, length$data) while length $data < 12;
                        ($len) = unpack 'x4L', $data;
                        $box->read($data,12+$len-length$data,length$data) while length $data < 12+$len;
                        $h->write($data);
                        close $h;
                        last;
                    }
                    sleep 0.1;
                }
            } else {
                while($h->read($data,1024) > 0) { 0; }
            }
            close $h;
        }
        close $l;
        close $box;
        exit;
    }


    local $server = "127.0.0.1:1111";
    local $opts{raise} = 0;
    local $opts{timeout} = 0.1;
    local $opts{select_timeout} = 0.1;

    my $try = 2;

    my $counter = 0;
    local $SIG{__WARN__} = countwarn(qr/timed? ?out/i, \$counter);

    my $box = $CLASS->new(def_param('l&SSLL&'));

    sleep 1;

    is_deeply $box->Select(13,{ want => "arrayref", raise => 1 }), [[13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789']], "timeout2/select/raise/sync";
    ok !$box->Error, "timeout2/select/raise/sync/error";
    ok !$box->ErrorStr, "timeout2/select/raise/sync/errorstr";
    ok $counter == $try, "timeout2/select/raise/sync/counter";
    $counter = 0;

    is_deeply $box->Select(13,{ want => "arrayref", raise => 0 }), [[13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789']], "timeout2/select/noraise/sync";
    ok !$box->Error, "timeout2/select/noraise/sync/error";
    ok !$box->ErrorStr, "timeout2/select/noraise/sync/errorstr";
    ok $counter == $try, "timeout2/select/noraise/sync/counter";
    $counter = 0;

    my $continuation = $box->Select(13,{ return_fh => 1, raise => 0, want => 'arrayref' });
    ok $continuation, "timeout2/select/continuation";
    is_deeply $continuation->{continue}->(), [[13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789']], "timeout2/select/continuation/result";
    ok !$box->Error, "timeout2/select/continuation/error";
    ok !$box->ErrorStr, "timeout2/select/continuation/errorstr";
    ok $counter == 0, "timeout2/select/continuation/counter";
    $counter = 0;


  SKIP:{
        skip "AnyEvent not found", 5 unless $ANYEVENT;

        AnyEvent->now_update;
        my $cv = AnyEvent->condvar;
        $cv->begin;
        ok $box->Select(13,{ callback => sub {
                                  my ($res) = @_;
                                  $cv->end;
                                  is_deeply $res, [[13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789']], "timeout2/select/async/noraise/cb";
                                  ok !$box->Error, "timeout2/select/async/noraise/cb/error";
                                  ok !$box->ErrorStr, "timeout2/select/async/noraise/cb/errorstr";
                              }}), "timeout2/select/async/noraise";

        $cv->recv;
        ok $counter == $try, "timeout2/select/async/noraise/counter";
        $counter = 0;
    }

    kill 'TERM', $pid;
};


$box = $CLASS->new(def_param);
ok $box->isa($CLASS), 'connect';
cleanup 13;

ok $box->Insert(13, q/some_email@test.mail.ru/, 1, 2, 3, 4), 'insert';
ok $box->Insert(13, q/some_email@test.mail.ru/, 2, 2, 3, 4), 'replace';

ok $box->Update(13, 3 => 1) == 1, 'update of some field';
is_deeply scalar $box->Select(13), [13, 'some_email@test.mail.ru', 2, 1, 3, 4], 'select/update';

cleanup 14;
ok $box->Insert(14, 'aaa@test.mail.ru', 0, 0, 1, 1), 'insert';

is_deeply scalar $box->Select(14), [14, 'aaa@test.mail.ru', 0, 0, 1, 1], 'select';
is_deeply scalar $box->Select('aaa@test.mail.ru', {use_index => 'primary_email'}), [14, 'aaa@test.mail.ru', 0, 0, 1, 1], 'select';


ok $box->Update(14, 2 => 2), 'update of some field';


is_deeply scalar $box->Select(14), [14, 'aaa@test.mail.ru', 2, 0, 1, 1], 'select';
is_deeply scalar $box->Select('aaa@test.mail.ru', {use_index => 'primary_email'}), [14, 'aaa@test.mail.ru', 2, 0, 1, 1], 'select';



$box = $CLASS->new(def_param);
ok $box->isa($CLASS), 'connect';



for (1..3) {
    %MR::IProto::sockets = ();
    $box = $CLASS->new(def_param);
    ok $box->isa($CLASS), 'connect';

    cleanup 14;

    ok $box->Insert(14, 'aaa@test.mail.ru', 0, 0, 1, 1), 'insert';

    ok $box->Update(14, 2 => 2), 'update of field';
}

# interfaces
#            0   1                   2  3  4  5
my @tuple = (14, 'aaa@test.mail.ru', 0, 0, 1, 0);
my $id = $tuple[0];
my $email = $tuple[1];

cleanup 14;
ok $box->Insert(@tuple), 'insert';

is_deeply scalar $box->Select($id), \@tuple, 'select';


### Bit ops

# zero namespace
$box->Bit($id, 5, bit_set => (1 << 15));
$tuple[5] |= (1 << 15);
is_deeply scalar $box->Select($id), \@tuple, 'bit set';

$box->Bit($id, 5, bit_clear => (1 << 15));
$tuple[5] &= ~(1 << 15);
is_deeply scalar $box->Select($id), \@tuple, 'bit clear';

$box->Bit($id, 5, bit_set => (1 << 15), bit_clear => (1 << 16));
$tuple[5] |= (1 << 15);
$tuple[5] &= ~(1 << 16);
is_deeply scalar $box->Select($id), \@tuple, 'bit_set + bit_clear';

$box->Bit($id, 5, set => 4095, bit_set => (1 << 5), bit_clear => (1 << 6));
$tuple[5] = 4095;
$tuple[5] |= (1 << 5);
$tuple[5] &= ~(1 << 6);
is_deeply scalar $box->Select($id), \@tuple, 'set + bit_set + bit_clear';

$box->Bit($id, 5, set => 123);
$tuple[5] = 123;
is_deeply scalar $box->Select($id), \@tuple, 'set via Bit';




## Num ops

# zero namespace
$box->Num($id, 5, num_add => 1);
$tuple[5] += 1;
is_deeply scalar $box->Select($id), \@tuple, 'num_add';

$box->Num($id, 5, num_sub => 1);
$tuple[5] -= 1;
is_deeply scalar $box->Select($id), \@tuple, 'num_sub';

$box->Num($id, 5, set => 123);
$tuple[5] = 123;
is_deeply scalar $box->Select($id), \@tuple, 'set via Num';

$box->Num($id, 5, set => 4095, num_add => 5, num_sub => 10);
$tuple[5] = 4095;
$tuple[5] += 5;
$tuple[5] -= 10;
is_deeply scalar $box->Select($id), \@tuple, 'set + num_add + num_sub';


### Bit & Num opt parse
throws_ok sub { $box->Bit($id, 5, update => 123) }, qr/unknown op 'update'/, 'bad op for Bit';
throws_ok sub { $box->Bit($id, 5, hxxxxx => 123) }, qr/unknown op 'hxxxxx'/, 'bad op for Bit';
throws_ok sub { $box->Num($id, 5, update => 123) }, qr/unknown op 'update'/, 'bad op for Num';
throws_ok sub { $box->Num($id, 5, hxxxxx => 123) }, qr/unknown op 'hxxxxx'/, 'bad op for Num';


### AndXorAdd

$box->AndXorAdd($id, 5, 4095, 5, 10);
$tuple[5] &= 4095;
$tuple[5] ^= 5;
$tuple[5] += 10;
is_deeply scalar $box->Select($id), \@tuple, 'AndXorAdd namespace=1';


### key parser
throws_ok sub { my $a = $box->Select('some@test.mai;.ru') }, qr/not numeric key/, 'validation of $key';
throws_ok sub { my $a = $box->Num('some@test.mai;.ru', 5, num_add => 1) }, qr/not numeric key/, 'validation of $key';
throws_ok sub { my $a = $box->Delete('some@test.mai;.ru') }, qr/not numeric key/, 'validation of $key';

throws_ok sub { my $a = $box->Select('1.1') }, qr/not numeric key/, 'validation of $key';
throws_ok sub { my $a = $box->Num('1.1', 5, num_add => 1) }, qr/not numeric key/, 'validation of $key';
throws_ok sub { my $a = $box->Delete('1.1') }, qr/not numeric key/, 'validation of $key';

throws_ok sub { my $a = $box->Select('') }, qr/not numeric key/, 'validation of $key';
throws_ok sub { my $a = $box->Num('', 5, num_add => 1) }, qr/not numeric key/, 'validation of $key';
throws_ok sub { my $a = $box->Delete('') }, qr/not numeric key/, 'validation of $key';


## Detete
cleanup $id;
ok $box->Insert(@tuple), 'insert';
ok $box->Delete($id), 'delete by id';
ok 0 == $box->Delete($id), 'delete by id';

## UpdateMulti
cleanup $id;
ok $box->Insert(@tuple), 'insert';

ok $box->UpdateMulti($id, ([5 => set => 1]) x 127), 'big update multi';
# CANT TEST throws_ok sub { $box->UpdateMulti($id, ([5 => set => 1]) x 128) }, ILL_PARAM, 'too big update multi';
throws_ok sub { $box->UpdateMulti($id, ([5 => set => 1]) x 129) }, qr/too many op/, 'too big update multi';
{
    my $box = $CLASS->new(def_param(q/l& SSL&/));
    my @tuple = @tuple;
    ok $box->isa($CLASS), 'connect';

    ok $box->UpdateMulti($id, map { [5 => set => 'x' x 127] } (1..127)), 'big update multi';
    $tuple[5] = 'x' x 127;
    is_deeply scalar $box->Select($id), \@tuple, 'result of update multi';

    # DISABLED since no BER IN BOX yet
    # ok $box->UpdateMulti($id, ([5 => set => 'x' x 6554])), 'very big update multi';
    # $tuple[5] = 'x' x 6554;
    # is_deeply scalar $box->Select($id), \@tuple, 'result of update multi';
}

{
    my $box = $CLASS->new(def_param(q/l& &&&&/));
    my @tuple = @tuple;
    my $id = $tuple[0];
    ok $box->isa($CLASS), 'connect';

    ok $box->UpdateMulti($id, [2 => set => 'ab'], [5 => set => 'z' x 127]), 'update multi no teplate';
    $tuple[2] = 'ab';
    $tuple[5] = 'z' x 127;
    my @r = @{$box->Select($id)};
    is_deeply scalar [@r[2,5]], [@tuple[2,5]] , 'result of update multi';
}

throws_ok sub { $box->UpdateMulti($id, [5 => and_xor_add => [1,2,3,4]]) }, qr/bad op <and_xor_add>/, 'bad and_xor_add';
throws_ok sub { $box->UpdateMulti($id, [5 => and_xor_add => [1,2]]) }, qr/bad op <and_xor_add>/, 'bad and_xor_add';
throws_ok sub { $box->UpdateMulti($id, [5 => and_xor_add => 1]) }, qr/bad op <and_xor_add>/, 'bad and_xor_add';
throws_ok sub { $box->UpdateMulti($id, [1, 2, 3]) }, qr/bad op/, 'bad op';
throws_ok sub { $box->UpdateMulti($id, [1, 2]) }, qr/bad op/, 'bad op';
throws_ok sub { $box->UpdateMulti($id, '') }, qr/bad op/, 'bad op';

{
    my @tuple = (14, 'aaa@test.mail.ru', 0, 0, 1, 1);

    cleanup 14;
    cleanup 15;
    ok $box->Insert(@tuple), 'insert';

    my @op = ([4 => num_add => 300], [5 => num_sub => 100], [5 => set => 1414], [5 => bit_set => 1 | 2], [5 => bit_clear => 2]);
    ok $box->UpdateMulti($tuple[0], @op), 'update multi';
    my @tuple_new = @tuple;
    $tuple_new[4] += 300;
    $tuple_new[5] -= 100;
    $tuple_new[5] = 1414;
    $tuple_new[5] |= 1 | 2;
    $tuple_new[5] &= ~2;
    is_deeply scalar $box->Select(14), \@tuple_new, 'update multi';

    cleanup 14;
    ok $box->Insert(@tuple), 'insert';
    is_deeply scalar $box->UpdateMulti($tuple[0], @op, {want_result => 1}), \@tuple_new, 'update multi, want_result';

    ok 0 == $box->UpdateMulti(15, [4 => num_add => 300], [5 => num_sub => 100], [5 => set => 1414]), 'update multi of nonexist';
}


## Select
throws_ok sub { $box->Select(1) }, qr/void context/, 'select in void context';
throws_ok sub { my $a = $box->Select(1,2,3) }, qr/too many keys in scalar context/, 'too many keys in scalar context';

cleanup $tuple[0];
ok $box->Insert(@tuple);
is_deeply [$box->Select($tuple[0], $tuple[0])], [\@tuple, \@tuple], 'select in scalar context';
is_deeply scalar $box->Select($tuple[0], $tuple[0], {want => 'arrayref'}), [\@tuple, \@tuple], 'select want => arrrayref';

{
    my $box = $CLASS->new(
        { %{def_param()},
          hashify => sub {
              my ($namespace, $row) = @_;
              my $i = 1;
              foreach (@$row) {
                  my @tuple = @{$_};
                  $_ = {};
                  foreach my $k (@tuple) {
                      $_->{$i++} = $k;
                  }
              }
          },
      }
    );

    my $hash = {};
    my $i = 1;
    $hash->{$i++} = $_ foreach @tuple;

    is_deeply $box->Select($tuple[0]), $hash, 'select with hashify';
}

## Tree indexes
sub def_param1 {
    my $format = 'l&l';
    return { servers => $server,
             namespaces => [ {
                 indexes => [ {
                     index_name   => 'primary_num1',
                     keys         => [0],
                 }, {
                     index_name   => 'secondary_str2',
                     keys         => [1],
                 }, {
                     index_name   => 'secondary_complex',
                     keys         => [1, 2],
                 } ],
                 namespace     => 26,
                 format        => $format,
                 default_index => 'primary_num1',
             } ],
             %opts,
         }
}

$box = $CLASS->new(def_param1);
ok $box->isa($CLASS), 'connect';

my @tuple1 = (13, 'mail.ru', 123);
cleanup $tuple1[0];

my @tuple2 = (14, 'mail.ru', 456);
cleanup $tuple2[0];

ok $box->Insert(@tuple1);

is_deeply [$box->Select([[$tuple1[0]]])], [\@tuple1], 'select by primary_num1 index';
is_deeply [$box->Select([[$tuple1[1]]], { use_index => 'secondary_str2' })], [\@tuple1], 'select by secondary_str2 index';
is_deeply [$box->Select([[$tuple1[1], $tuple1[2]]], { use_index => 'secondary_complex' })], [\@tuple1], 'select by secondary_complex index';
is_deeply [$box->Select([[$tuple1[1]]], { use_index => 'secondary_complex' })], [\@tuple1], 'select by secondary_complex index, partial key';

ok $box->Insert(@tuple2);

is_deeply [$box->Select([[$tuple2[0]]])], [\@tuple2], 'select by primary_num1 index';
is_deeply [$box->Select([[$tuple1[1]]], { use_index => 'secondary_str2', limit => 2, offset => 0 })], [\@tuple1, \@tuple2], 'select by secondary_str2 index';
is_deeply [$box->Select([[$tuple2[1], $tuple2[2]]], { use_index => 'secondary_complex' })], [\@tuple2], 'select by secondary_complex index';

## Check index constrains
sub def_param_bad {
    my $format = 'l&&';
    return { servers => $server,
             spaces => [ {
                 indexes => [ {
                     index_name   => 'primary_num1',
                     keys         => [0],
                 }, {
                     index_name   => 'secondary_str2',
                     keys         => [1],
                 }, {
                     index_name   => 'secondary_complex',
                     keys         => [1, 2],
                 } ],
                 namespace     => 26,
                 format        => $format,
                 default_index => 'primary_num1',
             } ],
             %opts,
         }
}

$box = $CLASS->new(def_param_bad);
ok $box->isa($CLASS), 'connect';

my @tuple_bad = (13, 'mail.ru', '123');
cleanup $tuple_bad[0];
throws_ok sub { $box->Insert(@tuple_bad) }, ILL_PARAM, "index_constains/bad_field_type";


## Check unique tree index
sub def_param_unique {
    my $format = 'l&&&';
    return { servers => $server,
             namespaces => [ {
                 indexes => [ {
                     index_name   => 'id',
                     keys         => [0],
                 }, {
                     index_name   => 'email',
                     keys         => [1],
                 }, {
                     index_name   => 'firstname',
                     keys         => [2],
                 }, {
                     index_name   => 'lastname',
                     keys         => [3],
                 } , {
                     index_name   => 'fullname',
                     keys         => [2, 3]
                 } ],
                 space     => 27,
                 format        => $format,
                 default_index => 'id',
             } ],
             %opts,
         }
}

$box = $CLASS->new(def_param_unique);
ok $box->isa($CLASS), 'connect';

my $tuples = [ [1, 'rtokarev@corp.mail.ru', 'Roman', 'Tokarev'],
	       [2, 'vostrikov@corp.mail.ru', 'Yuri', 'Vostrikov'],
	       [3, 'aleinikov@corp.mail.ru', 'Roman', 'Aleinikov'],
	       [4, 'roman.s.tokarev@gmail.com', 'Roman', 'Tokarev'],
	       [5, 'vostrikov@corp.mail.ru', 'delamon', 'delamon'] ];

foreach my $tuple (@$tuples) {
	cleanup $tuple->[0];
}

foreach my $tuple (@$tuples) {
	if ($tuple == $tuples->[-1] || $tuple == $tuples->[-2]) {
		throws_ok sub { $box->Insert(@$tuple) }, INDEX_VIOLATION, "unique_tree_index/insert \'$tuple->[0]\'";
	} else {
		ok $box->Insert(@$tuple), "unique_tree_index/insert \'$tuple->[0]\'";
	}
}

my @res = $box->Select([map $_->[0], @$tuples], { limit => 100 });
foreach my $r (@res) {
	ok sub { return $r != $tuples->[-1] && $r != $tuples->[-2] };
}

my $flds;
my $lflds;
BEGIN{ $flds = [qw/ f1 f2 f3 f4 LL /] }
BEGIN{ $lflds = [qw/ l1 l2 l3 /] }
    {
        package TestBox;
        use MR::Tarantool::Box::Singleton;
        use base 'MR::Tarantool::Box::Singleton';

        BEGIN {
            __PACKAGE__->mkfields(@$flds);
            __PACKAGE__->mklongfields(@$lflds);
        }

        sub SERVER   { $server }
        sub REPLICAS { '' }

        sub SPACES   {[{
            space         => 27,
            indexes       => [ {
                index_name   => 'primary_id',
                keys         => [TUPLE_f1],
            } ],
            format        => 'l&$&(&$&)*',
            default_index => 'primary_id',
        }]}

    }

$box = 'TestBox';
#$box = $CLASS->new(def_param_flds);
#ok $box->isa($CLASS), 'connect';
do {
    my $tuples = [
        [1, "asdasdasd1", "qqq\xD0\x8Eqqq1", "ww\xD0\x8Eww1", "la\xD0\x8Elalala11", "la\xD0\x8Elala11", "lala11"],
        [2, "asdasdasd2", "qqq\xD0\x8Eqqq2", "ww\xD0\x8Eww2", "la\xD0\x8Elalala21", "la\xD0\x8Elala21", "lala21", "lalalala22", "lalala22", "lala22", "lalalala23", "lalala23", "lala23"],
        [3, "asdasdasd3", "qqq\xD0\x8Eqqq3", "ww\xD0\x8Eww3", "la\xD0\x8Elalala31", "la\xD0\x8Elala31", "lala31", "lalalala32", "lalala32", "lala32"],
        [4, "asdasdasd4", "qqq\xD0\x8Eqqq4", "ww\xD0\x8Eww4", "la\xD0\x8Elalala41", "la\xD0\x8Elala41", "lala41", "lalalala42", "lalala42", "lala42", "lalalala43", "lalala43", "lala43"],
        [5, "asdasdasd5", "qqq\xD0\x8Eqqq5", "ww\xD0\x8Eww5", "la\xD0\x8Elalala51", "la\xD0\x8Elala51", "lala51"],
    ];

    my $check = [];
    for my $tuple (@$tuples) {
        my $i = 0;
        Encode::_utf8_on($tuple->[2+$i*3]), ++$i while @$tuple > 1+$i*3;

        my $t = { zip @{[@$flds[0..($#$flds-1)]]}, @{[@$tuple[0..($#$flds-1)]]} };
        my $l = $t->{$flds->[-1]} = [];

        $i = 1;
        push(@$l, { zip @$lflds, @{[@$tuple[(1+$i*3)..(1+$i*3+2)]]} }), ++$i while @$tuple > 1+$i*3;

        push @$check, $t;
    }

    foreach my $tuple (@$tuples) {
        cleanup $tuple->[0];
    }

    foreach my $i (0..$#$tuples) {
        is_deeply [$box->Insert(@{$tuples->[$i]}, {want_inserted_tuple => 1})], [$check->[$i]], "flds/insert \'$tuples->[$i]->[0]\'";
    }

    is_deeply [$box->Select([[$tuples->[0]->[0]]])], [$check->[0]], 'select by primary_num1 index';

    my $res;
    is_deeply [$res=$box->Select([map {$_->[0]} @$tuples],{want=>'arrayref'})], [$check], 'select all';
    # print $res->[0]->{f3}, "\n";
    # print $check->[0]->{f3}, "\n";
    ok $res->[$_]->{f3}            eq $check->[$_]->{f3}, "utf8chk"                for 0..$#$tuples;
    ok $res->[$_]->{LL}->[0]->{l2} eq $check->[$_]->{LL}->[0]->{l2}, "utf8chklong" for 0..$#$tuples;

    is_deeply [$box->UpdateMulti($tuples->[2]->[0],[ $flds->[3] => set => $tuples->[2]->[3] ],{want_updated_tuple => 1})], [$check->[2]], 'update1';
    ok         $box->UpdateMulti($tuples->[2]->[0],[ $flds->[3] => set => $tuples->[2]->[3] ]), 'update2';
    is_deeply [$box->UpdateMulti($tuples->[2]->[0],[ 3          => set => $tuples->[2]->[3] ],{want_updated_tuple => 1})], [$check->[2]], 'update3';
    ok         $box->UpdateMulti($tuples->[2]->[0],[ 3          => set => $tuples->[2]->[3] ]), 'update4';

    is_deeply [$box->Delete($tuples->[$_]->[0],{want_deleted_tuple => 1})], [$check->[$_]], "delete$_" for 0..$#$tuples;
};



## Check u64 index
# note, that u64 keys are emulated via pack('ll') since default ubuntu perl doesn't support pack('q')
sub def_param_u64 {
    my $format = '&&&&';
    return { servers => $server,
             spaces => [ {
                 indexes => [ {
                     index_name   => 'id',
                     keys         => [0],
                 } ],
                 space         => 20,
                 format        => $format,
                 default_index => 'id',
             } ],
             %opts,
         }
}

$box = $CLASS->new(def_param_u64);
ok $box->isa($CLASS), 'connect';

$_->[0] = pack('ll', $_->[0], 0) foreach @$tuples;

foreach my $tuple (@$tuples) {
	cleanup $tuple->[0];
}

foreach my $tuple (@$tuples) {
    ok $box->Insert(@$tuple), "unique_tree_index/insert \'$tuple->[0]\'";
}

is_deeply($tuples, [$box->Select([map $_->[0], @$tuples])]);


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
