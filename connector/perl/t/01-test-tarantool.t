#!/usr/bin/perl

use warnings;
use strict;
use utf8;
use open qw(:std :utf8);
use lib qw(lib ../lib);

use Test::More tests    => 9;
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

