#!/bin/bash

set -e
shopt -s extglob
ulimit -c unlimited

test_dir=/var/tarantool_test
build_type=_test

kill_all () {
    test -d $test_dir && find $test_dir -type f -a -name \*.pid 2>/dev/null | while read pid_file; do
        test -f $pid_file && kill $(cat $pid_file)
    done
    sleep 1
    ps -eopid,cmd | grep [t]arantul
}

trap 'kill_all' EXIT

test_box() {
    BOX=localhost:33013 perl -MTest::Harness -e 'runtests(@ARGV)' mod/silverbox/t/box.pl
}

test_admin() {
        # FIXME: test for valid output
        cat <<-EOF | nc localhost 33015 > /dev/null
	help
	asdfasdfa
	show configuration
	show conf
	show slab
	show palloc
	show stat
	save snapshot
	quit
EOF
}

test_all() {
    test_box
    test_admin
}

lsn() {
    local port=${1:?}
    (echo -e "show info\nquit"; sleep 0.1) | nc localhost $port | perl -MYAML -e'($a) = Load(join q//, <>); print $a->{info}->{lsn},"\n"'
}

start_tarantool() {
    local type=${1:?}
    shift
    $build_type/tarantool_silverbox --config $test_dir/$type.cfg $@
    sleep 1
    test -f $test_dir/$type.pid
}

kill_tarantool() {
    local type=${1:?}
    kill $(cat $test_dir/$type.pid)
    sleep 1
    test ! -f $test_dir/$type.pid
}

write_config() {
    local file_name=${1:?}
    local secondary_port=${2:?}
    local admin_port=${3:?}

    cat <<-EOF > $test_dir/$file_name.cfg
	log_level = 4
	slab_alloc_arena = 0.1
	
	work_dir = "$test_dir"
	snap_dir = "storage/snap"
	wal_dir = "storage/wal"
	logger = "cat >> $file_name.log"
	pid_file = "$file_name.pid"

	primary_port = 33013
	secondary_port = $secondary_port
	admin_port = $admin_port

        rows_per_wal = 50
	wal_fsync_delay	= 0.5

	local_hot_standby = 1
	# remote_hot_standby = 1
	wal_feeder_ipaddr = "10.3.1.192"
	wal_feeder_port = 24444
	
	namespace[0].enabled = 1
	namespace[0].index[0].type = "NUM"
	namespace[0].index[0].key_fields[0].fieldno = 0
	namespace[0].index[1].type = "STR"
	namespace[0].index[1].key_fields[0].fieldno = 1
	
	namespace[3].enabled = 1
	namespace[3].index[0].type = "NUM"
	namespace[3].index[0].key_fields[0].fieldno = 0
	
	namespace[5].enabled = 1
	namespace[5].index[0].type = "NUM"
	namespace[5].index[0].key_fields[0].fieldno = 0
	
	namespace[11].enabled = 1
	namespace[11].index[0].type = "NUM"
	namespace[11].index[0].key_fields[0].fieldno = 0
	
	namespace[19].enabled = 1
	namespace[19].index[0].type = "STR"
	namespace[19].index[0].key_fields[0].fieldno = 0
	
	namespace[22].enabled = 1
	namespace[22].index[0].type = NUM
	namespace[22].index[0].key_fields[0].fieldno = 0
	
	namespace[23].enabled = 1
	namespace[23].index[0].type = NUM
	namespace[23].index[0].key_fields[0].fieldno = 0
	
	namespace[24].enabled = 1
	# namespace[24].dual_to = 25
	namespace[24].index[0].type = "NUM"
	namespace[24].index[0].key_fields[0].fieldno = 0
	namespace[24].index[1].type = "STR"
	namespace[24].index[1].key_fields[0].fieldno = 1
	
	#namespace[25].enabled = 0
	#namespace[25].dual_to = 24
	#namespace[25].index[0].type = "STR"
	#namespace[25].index[0].key_fields[0].fieldno = 1

	namespace[26].enabled = 1
	namespace[26].index[0].type = "NUM"
	namespace[26].index[0].key_fields[0].fieldno = 0
	namespace[26].index[1].type = "TREE_STR"
	namespace[26].index[1].key_fields[0].fieldno = 1
	namespace[26].index[2].type = "TREE_STR"
	namespace[26].index[2].key_fields[0].fieldno = 1
	namespace[26].index[2].key_fields[1].fieldno = 2

EOF
}

prepare() {
    rm -rf /var/core/tarantool*
    rm -rf $test_dir/!(storage) $test_dir/storage/{snap,wal} $test_dir/storage/file
    mkdir -p $test_dir/storage/{snap,wal}

    # rm -rf $build_type
    test -d $build_type && find $build_type -name \*.gcda -print0 | xargs -0 rm -vf
    make $build_type/tarantool_silverbox
}

generate_coverage() {
    rm -rf lcov
    mkdir -p lcov
    rsync -r $build_type/ lcov
    rsync -r core/ lcov/core
    rsync -r mod/ lcov/mod
    rsync -r third_party/ lcov/third_party

    lcov -c -d lcov/core -d lcov/mod -o lcov/tarantool.lcov.tmp4
    lcov -r lcov/tarantool.lcov.tmp4 '*/third_party/*' -o lcov/tarantool.lcov.tmp3
    lcov -r lcov/tarantool.lcov.tmp3 '*/mod/silverbox/memcached.c' -o lcov/tarantool.lcov.tmp2
    lcov -r lcov/tarantool.lcov.tmp2 '/usr/include/*' -o lcov/tarantool.lcov.tmp
    lcov -r lcov/tarantool.lcov.tmp '*/core/admin.c' -o lcov/tarantool.lcov
    genhtml -o lcov lcov/tarantool.lcov
}


prepare 

write_config master 33014 33015
write_config slave 33016 33017

# initialize storage
$build_type/tarantool_silverbox --config $test_dir/master.cfg --init_storage


# repeat test with different log levels
start_tarantool master --daemonize
test_all
kill_tarantool master

# # test handling of wal overflow
# start_tarantool master --silverbox --daemonize
# perl -Imod/silverbox/t -MTBox -e'eval { $box->Insert(1, q/aaa/, 2, 3, 4, 5); $box->Delete(1);}; exit(1) if $@;'
# (dd if=/dev/zero of=$test_dir/storage/file 2>/dev/null)
# perl -Imod/silverbox/t -MTBox -e'eval { $box->Insert(1, q/aaa/, 2, 3, 4, 5, q/a/ x 65000); $box->Delete(1);}; exit(0) if $@;'
# rm $test_dir/storage/file
# kill_tarantool master


# only one core in 60 minutes
start_tarantool master --daemonize -v -v
rm -f /var/core/tarantool*
[ $(echo -e "save coredump\nq" | nc localhost 33015 | sed 's/[\r\n]//g') == "ok" ]
[ $(echo -e "save coredump\nq" | nc localhost 33015 | sed 's/[\r\n]//g') == "ok" ]
[ $(ls /var/core/tarantool* 2>/dev/null | wc -l) -eq 1 ]
rm -f /var/core/tarantool*
kill_tarantool master

# start with absolute path to config
$build_type/tarantool_silverbox --config $test_dir/master.cfg --daemonize
sleep 1
test_all
kill_tarantool master

# repeat test with different log levels
start_tarantool master --daemonize
test_all
kill_tarantool master

start_tarantool master --daemonize -v
test_all
kill_tarantool master

start_tarantool master --daemonize -v -v
test_all
kill_tarantool master

start_tarantool master --daemonize -v -v -v
test_all
kill_tarantool master

start_tarantool master --daemonize -v -v -v -v
test_all
kill_tarantool master

# cat snap/xlog
$build_type/tarantool_silverbox --cat $(ls -tr $test_dir/storage/wal/*.xlog | tail -n1) >/dev/null
$build_type/tarantool_silverbox --cat $(ls -tr $test_dir/storage/snap/*.snap | tail -n1) >/dev/null


# # test local hot standby
start_tarantool master --daemonize
start_tarantool slave --daemonize -v -v -v
test_box
[ $(lsn 33015) == $(lsn 33017) ]
test_box
[ $(lsn 33015) == $(lsn 33017) ]
kill_tarantool master
kill_tarantool slave

generate_coverage


