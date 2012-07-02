#!/usr/bin/perl

use warnings;
use strict;
use utf8;
use open qw(:std :utf8);
use lib qw(lib ../lib);

use Test::More tests    => 16;
use Encode qw(decode encode);


BEGIN {
    # Подготовка объекта тестирования для работы с utf8
    my $builder = Test::More->builder;
    binmode $builder->output,         ":utf8";
    binmode $builder->failure_output, ":utf8";
    binmode $builder->todo_output,    ":utf8";

    use_ok 'Test::Tarantool';
    use_ok 'File::Spec::Functions', 'catfile';
    use_ok 'File::Basename', 'dirname';
    use_ok 'MR::Tarantool::Box';
    use_ok 'Time::HiRes', 'time';
}


my $data_dir = catfile dirname(__FILE__), 'data';
ok -d $data_dir, "-d $data_dir";

my $cfg = catfile $data_dir, 'tnt.cfg';


ok -r $cfg, "-r $cfg";

my $t1 = Test::Tarantool->run(cfg => $cfg, script_dir => $data_dir);
my $t2 = Test::Tarantool->run(cfg => $cfg, script_dir => $data_dir);



ok $t1, 'test tarantool 1';
ok $t2, 'test tarantool 2';
ok $t1->started, 'tarantool 1 is started';
ok $t2->started, 'tarantool 2 is started';

my $spaces =  [
        {
            indexes => [
                {
                    index_name => 'i0',
                    keys    => [ 0 ]
                },
                {
                    index_name => 'i1',
                    keys    => [ 1 ]
                },
                {
                    index_name => 'i2',
                    keys    => [ 2 ]
                },
            ],

            default_index => 'i0',
            fields => [ 'id', 'name', 'value' ],
            space  => 0,
            name   => 'test',
            format => 'L$L'
        }
    ]
;



my $box1 = MR::Tarantool::Box->new({
    servers => '127.0.0.1:' . $t1->primary_port,
    name    => 'Test1',
    spaces  => $spaces
});

$box1->Insert(1, 'first', 1);
$box1->Call(tst_rand_init => [], { unpack_format => '$'});

my $box2 = MR::Tarantool::Box->new({
    servers => '127.0.0.1:' . $t2->primary_port,
    name    => 'Test1',
    spaces  => $spaces
});

$box2->Insert(1, 'second', 1);
$box2->Call(tst_rand_init => [], { unpack_format => '$'});


my $box_union = MR::Tarantool::Box->new({
    servers => join(',',
        '127.0.0.1:' . $t1->primary_port,
        '127.0.0.1:' . $t2->primary_port
    ),
    name    => 'Test',
    spaces  => $spaces
});

ok $box_union, 'connector is created';
ok $box1, 'connector is created';
ok $box2, 'connector is created';


my %resps;
for (1 .. 20) {
    my $tuples = $box_union->Call(
        tst_server_name => [], { unpack_format => '$' }
    );
    $resps{ $tuples->[0][0] }++;
}

is 1, scalar keys %resps, 'one server was used for all requests';
is $resps{first} || $resps{second}, 20, 'all requests were handled';

