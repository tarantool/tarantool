use strict;
use warnings;
BEGIN {
    sub mPOP::Config::GetValue ($) {
        die;
    }
}
use FindBin qw($Bin);
use lib "$Bin";
use TBox ();

use Test::More qw/no_plan/;
use Test::Exception;

use constant ILL_PARAM => qr/MR::SilverBox: Error 00000202/;
use constant TOO_BIG_FIELD => qr/too big field/;

my $box;
my $server = (shift || $ENV{BOX}) or die;

sub cleanup ($) {
    my ($id) = @_;
    ok defined $box->Delete($id), 'delete of possible existing record';
    ok $box->Delete($id) == 0, 'delete of non existing record';
}

sub def_param  {
    my $format = shift || 'l& SSLL';
    return { servers => $server,
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
             } ]}
}
$box = MR::SilverBox->new(def_param);
ok $box->isa('MR::SilverBox'), 'connect';

cleanup 13;
ok $box->Insert(13, q/some_email@test.mail.ru/, 1, 2, 3, 4), 'insert';
ok $box->Insert(13, q/some_email@test.mail.ru/, 2, 2, 3, 4), 'replace';
ok $box->Update(13, 3 => 1) == 1, 'update of some field';

cleanup 14;
ok $box->Insert(14, 'aaa@test.mail.ru', 0, 0, 1, 1), 'insert';

is_deeply scalar $box->Select(14), [14, 'aaa@test.mail.ru', 0, 0, 1, 1], 'select';
is_deeply scalar $box->Select('aaa@test.mail.ru', {use_index => 'primary_email'}), [14, 'aaa@test.mail.ru', 0, 0, 1, 1], 'select';


ok $box->Update(14, 2 => 2), 'update of some field';


is_deeply scalar $box->Select(14), [14, 'aaa@test.mail.ru', 2, 0, 1, 1], 'select';
is_deeply scalar $box->Select('aaa@test.mail.ru', {use_index => 'primary_email'}), [14, 'aaa@test.mail.ru', 2, 0, 1, 1], 'select';



$box = MR::SilverBox->new(def_param);
ok $box->isa('MR::SilverBox'), 'connect';



for (1..3) {
    %MR::IProto::sockets = ();
    $box = MR::SilverBox->new(def_param);
    ok $box->isa('MR::SilverBox'), 'connect';

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

$box->Bit($id, 5, set => 4095, bit_set => (1 << 5), bit_clear => (1 << 6), {update_flags => 1});
$tuple[5] = 4095;
$tuple[5] |= (1 << 5);
$tuple[5] &= ~(1 << 6);
is_deeply scalar $box->Select($id), \@tuple, 'set + bit_set + bit_clear';

$box->Bit($id, 5, set => 123, {update_flags => 1});
$tuple[5] = 123;
is_deeply scalar $box->Select($id), \@tuple, 'set via Bit';




## Num ops

# zero namespace
$box->Num($id, 5, num_add => 1, {update_flags => 1});
$tuple[5] += 1;
is_deeply scalar $box->Select($id), \@tuple, 'num_add';

$box->Num($id, 5, num_sub => 1, {update_flags => 1});
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

$box->AndXorAdd($id, 5, 4095, 5, 10, {update_flags => 1});
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

ok $box->UpdateMulti($id, ([5 => set => 1]) x 127, {update_flags => 1}), 'big update multi';
# CANT TEST throws_ok sub { $box->UpdateMulti($id, ([5 => set => 1]) x 128) }, ILL_PARAM, 'too big update multi';
throws_ok sub { $box->UpdateMulti($id, ([5 => set => 1]) x 129) }, qr/too many op/, 'too big update multi';
{
    my $box = MR::SilverBox->new(def_param(q/l& SSL&/));
    my @tuple = @tuple;
    ok $box->isa('MR::SilverBox'), 'connect';

    ok $box->UpdateMulti($id, map { [5 => set => 'x' x 127] } (1..127)), 'big update multi';
    $tuple[5] = 'x' x 127;
    is_deeply scalar $box->Select($id), \@tuple, 'result of update multi';

    # DISABLED since no BER IN BOX yet
    # ok $box->UpdateMulti($id, ([5 => set => 'x' x 6554])), 'very big update multi';
    # $tuple[5] = 'x' x 6554;
    # is_deeply scalar $box->Select($id), \@tuple, 'result of update multi';
}

{
    my $box = MR::SilverBox->new(def_param(q/l& &&&&/));
    my @tuple = @tuple;
    my $id = $tuple[0];
    ok $box->isa('MR::SilverBox'), 'connect';

    ok $box->UpdateMulti($id, [2 => set => 'ab'], [5 => set => 'z' x 127], {update_flags => 1}), 'update multi no teplate';
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
    ok $box->UpdateMulti($tuple[0], @op, {update_base => 1, update_flags => 1}), 'update multi';
    my @tuple_new = @tuple;
    $tuple_new[4] += 300;
    $tuple_new[5] -= 100;
    $tuple_new[5] = 1414;
    $tuple_new[5] |= 1 | 2;
    $tuple_new[5] &= ~2;
    is_deeply scalar $box->Select(14), \@tuple_new, 'update multi';

    cleanup 14;
    ok $box->Insert(@tuple), 'insert';
    is_deeply scalar $box->UpdateMulti($tuple[0], @op, {update_base => 1, update_flags => 1, want_result => 1}), \@tuple_new, 'update multi, want_result';

    ok 0 == $box->UpdateMulti(15, [4 => num_add => 300], [5 => num_sub => 100], [5 => set => 1414], {update_base => 1, update_flags => 1}), 'update multi of nonexist';
}


## Select
throws_ok sub { $box->Select(1) }, qr/void context/, 'select in void context';
throws_ok sub { my $a = $box->Select(1,2,3) }, qr/too many keys in scalar context/, 'too many keys in scalar context';

cleanup $tuple[0];
ok $box->Insert(@tuple);
is_deeply [$box->Select($tuple[0], $tuple[0])], [\@tuple, \@tuple], 'select in scalar context';
is_deeply scalar $box->Select($tuple[0], $tuple[0], {want => 'arrayref'}), [\@tuple, \@tuple], 'select want => arrrayref';

{
    my $box = MR::SilverBox->new(
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
