#!/bin/bash
#==============================================================================#
# PHP tarantool test suite runner
#==============================================================================#

#------------------------------------------------------------------------------#
# Constants
#------------------------------------------------------------------------------#

# Success constant
SUCCESS=0
# Failure constant
FAILURE=1

# test runner etc directory
TEST_RUNNER_ETC_DIR="etc"
# test runner var directory
TEST_RUNNER_VAR_DIR="var"
# test runner var directory
TEST_RUNNER_LIB_DIR="lib"

# php.ini file
PHP_INI="$TEST_RUNNER_ETC_DIR/php.ini"

# Tarantool/box binary file name
TARANTOOL_BOX_BIN="tarantool_box"
# Tarantool/box configuration file
TARANTOOL_BOX_CFG="$TEST_RUNNER_ETC_DIR/tarantool_box.cfg"
# Tarantool/box pid file
TARANTOOL_BOX_PID="$TEST_RUNNER_VAR_DIR/tarantool_box.pid"


#------------------------------------------------------------------------------#
# Suite runner functions
#------------------------------------------------------------------------------#

# initialize suite
function init_suite()
{
	mkdir -p $TEST_RUNNER_VAR_DIR

	# check TARANTOOL_HOME variable
	if [ ! -z $TARANTOOL_HOME ]; then
		# Use user-defined path
		tarantool_bin="$TARANTOOL_HOME/$TARANTOOL_BOX_BIN"
    else
		# try to find by standard paths
		tarantool_bin="$TARANTOOL_BOX_BIN"
	fi

	# check binary
	if ! which $tarantool_bin > /dev/null 2>&1; then
		echo "can't found Tarantool/Box binary file"
		exit $FAILURE
	fi

	# check pear
	if ! which pear > /dev/null 2>&1; then
		echo "can't found pear"
		exit $FAILURE
	fi

	# check tarantool module library
	if [ ! -f ../modules/tarantool.so ]; then
		echo "can't found tarantool module library"
		exit $FAILURE
	fi
	if [ -f $TEST_RUNNER_LIB_DIR/tarantool.so ]; then
		rm $TEST_RUNNER_LIB_DIR/tarantool.so
	fi
	ln -s ../../modules/tarantool.so $TEST_RUNNER_LIB_DIR

	if [ -f $TEST_RUNNER_VAR_DIR/init.lua ]; then
		rm $TEST_RUNNER_VAR_DIR/init.lua
	fi
	ln -s ../$TEST_RUNNER_LIB_DIR/lua/sorted_array.lua $TEST_RUNNER_VAR_DIR/init.lua

	return $SUCCESS
}

# initialize tarantool's storage
function tarantool_init_storage()
{
	$tarantool_bin --init-storage -c $TARANTOOL_BOX_CFG 1> /dev/null 2>&1
	return $SUCCESS
}

# start tarantool
function tarantool_start()
{
	if [ -f $TARANTOOL_BOX_PID ]; then
		tarantool_stop
		tarantool_cleanup
	fi

	# run tarantool to background
	$tarantool_bin -c $TARANTOOL_BOX_CFG &
	# wait pid file
	for i in {1..500}; do
		if [ -f $TARANTOOL_BOX_PID ]; then
			break
		fi
		sleep 0.01
	done

	if [ ! -f $TARANTOOL_BOX_PID ]; then
		echo "error: can't start tarantool"
		tarantool_cleanup
		exit $FAILURE
	fi

	return $SUCCESS
}

# stop tarantool
function tarantool_stop()
{
	if [ ! -f $TARANTOOL_BOX_PID ]; then
		return $SUCCESS
	fi

	# get tarantool pid form pid file
	pid=`cat $TARANTOOL_BOX_PID`
	# kill process via SIGTERM
	kill -TERM $pid 1> /dev/null 2>&1

	for i in {1..500}; do
		if [ ! -f $TARANTOOL_BOX_PID ]; then
			# tarantool successfully stopped
			return $SUCCESS
		fi
		sleep 0.01
	done

	if [ -f $TARANTOOL_BOX_PID ]; then
		kill -KILL $pid 1> /dev/null 2>&1
	fi

	return $SUCCESS
}

# clean-up tarantool
function tarantool_cleanup()
{
	# delete pid
	rm -f $TEST_RUNNER_VAR_DIR/*.pid
	# delete xlogs
	rm -f $TEST_RUNNER_VAR_DIR/*.xlog
	# delete snaps
	rm -f $TEST_RUNNER_VAR_DIR/*.snap
}


#------------------------------------------------------------------------------#
# run test scrip body
#------------------------------------------------------------------------------#

#
# initialize
#

printf "initialize tarantool ... "
# initializing suite
init_suite
# initializing storage
tarantool_init_storage
printf "done\n"

printf "starting tarantool ... "
# start tarantool
tarantool_start
printf "done\n"


#
# run
#

printf "\n"
printf "================================= PHP test ===============================\n"

# running pear's regression test scrips
PHPRC=$PHP_INI pear run-tests $1

printf "==========================================================================\n"
printf "\n"

#
# stop & clean-up
#

printf "stopping tarantool ... "
# stop tarantool
tarantool_stop
# clean-up tarantool
tarantool_cleanup
printf "done\n"

#
# Local variables:
# tab-width: 4
# c-basic-offset: 4
# End:
# vim600: noet sw=4 ts=4 fdm=marker
# vim<600: noet sw=4 ts=4
#
