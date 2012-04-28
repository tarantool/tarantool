#!/bin/sh

#
# Copyright (C) 2012 Mail.RU
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

#
# Tarantool instance expansion script
#

prompt_name=`basename $0`

act_prompt=1
act_status=0
act_debug=0
act_dry=0
instance_current=0

error() {
	echo "$prompt_name error: $*" 1>&2
	exit 1
}

log() {
	echo "$prompt_name: $*"
}

usage() {
	echo "Tarantool expansion script: add more Tarantool instances."
	echo "usage: tarantool_expand.sh [options] <instances>"
	echo
	echo "  --prefix <path>       installation path (/usr/local)"
	echo "  --prefix_etc <path>   installation etc path (/etc)"
	echo "  --prefix_var <path>   installation var path (/var)"
	echo
	echo "  --status              display deployment status"
	echo "  --dry                 don't create anything, show commands"
	echo
	echo "  --debug               show commands"
	echo "  --yes                 don't prompt"
	echo "  --help                display this usage"
	echo
	exit $1
}

is_num_positive() {
	num=$1
	[ $num -eq $num ] 2>/dev/null && [ $num -ge 0 ] 2>/dev/null || \
		return 1
}

rollback_instance() {
	id=$1
	workdir="${prefix_var}/tarantool_box$id"
	config="${prefix}/etc/tarantool_box$id.cfg"
	rm -rf $workdir
	rm -f $config
	rm -f "${prefix}/bin/tarantool_box$id.sh"
	rm -f "${prefix_etc}/init.d/tarantool_box$id"
}

rollback() {
	log ">>>> rolling back changes"
	start=`expr $deploy_current + 1`
	for instance in `seq $start $instance_current`; do
		rollback_instance $instance
	done
	exit 1
}

try() {
	cmd="$*"
	[ $act_debug -gt 0 ] && log "$cmd"
	if [ $act_dry -eq 0 ]; then
		eval "$cmd"
		if [ $? -gt 0 ]; then
			rollback
		fi
	fi
}

deploy_instance() {
	id=$1
	workdir="${prefix_var}/tarantool_box$id"
	config="${prefix}/etc/tarantool_box$id.cfg"

	log ">>>> deploying instance $id"

	# setting up work environment
	try "mkdir -p $workdir/logs"

	# setting up startup snapshot
	try "cp \"${prefix}/share/tarantool/00000000000000000001.snap\" $workdir"
	try "chown tarantool:tarantool -R $workdir"

	# setting up configuration file
	try "cp \"${prefix}/etc/tarantool.cfg\" $config"
	try 'echo work_dir = \"$workdir\" >> $config'
	try 'echo username = \"tarantool\" >> $config'
	try 'echo logger = \"cat - \>\> logs/tarantool.log\" >> $config'

	# setting up wrapper
	try "ln -s \"${prefix}/bin/tarantool_multi.sh\" \"${prefix}/bin/tarantool_box$id.sh\""

	# setting up startup script
	try "ln -s \"${prefix_etc}/init.d/tarantool_box\" \"${prefix_etc}/init.d/tarantool_box$id\""
}

deploy() {
	start=`expr $deploy_current + 1`
	for instance in `seq $start $deploy_count`; do
		instance_current=$instance
		deploy_instance $instance
	done
}

commit() {
	log ">>>> updating deploy config"
	try "echo $deploy_count > $deploy_cfg"
}

# processing command line arguments
if [ $# -eq 0 ]; then
	usage 1
fi

deploy_count=0
while [ $# -ge 1 ]; do
	case $1 in
		--yes) act_prompt=0 ; shift 1 ;;
		--prefix) prefix=$2 ; shift 2 ;;
		--prefix_var) prefix_var=$2 ; shift 2 ;;
		--prefix_etc) prefix_etc=$2 ; shift 2 ;;
		--dry) act_dry=1 ; act_debug=1 ; shift 1 ;;
		--debug) act_debug=1; shift 1 ;;
		--status) act_status=1 ; shift 1 ;;
		--help) usage 0 ; shift 1 ;;
		*) deploy_count=$1 ; shift 1 ; break ;;
	esac
done

set ${prefix:="/usr/local"}
set ${prefix_var:="/var"}
set ${prefix_etc:="/etc"}

deploy_cfg="${prefix}/etc/tarantool_deploy.cfg"
deploy_exists=0
deploy_current=0

# checking deployment configuration file
if [ -f $deploy_cfg ]; then
	deploy_exists=1
	deploy_current=`cat $deploy_cfg`
	[ $? -eq 0 ] || error "failed to read deploy config"
	# validating instance number
	is_num_positive $deploy_current
	[ $? -eq 0 ] || error "bad deploy config instance number"
	# dont' change deploy if it said so in configuration file
	if [ $deploy_current -eq 0 ]; then
		log "skipping deploy setup (cancel by config)"
		exit 0
	fi
fi

# displaying status
if [ $act_status -ne 0 ]; then
	if [ $deploy_exists -eq 0 ]; then
		log "no tarantool instances found."
	else
		log "$deploy_current tarantool instances deployed"
	fi
	exit 0
fi

# validating instance number
is_num_positive $deploy_count
[ $? -eq 0 ] && [ $deploy_count -gt 0 ] || error "bad instance number"

if [ $deploy_count -le $deploy_current ]; then
	error "expand only is supported (required instances number $deploy_count" \
	      "is lower/equal than deployed $deploy_current)"
fi

# asking permission to continue
if [ $act_prompt -eq 1 ]; then
	[ $act_dry -ne 0 ] && log "(dry mode)"
	log "About to extend tarantool instances from $deploy_current to $deploy_count."
	log "Continue? [n/y]"
	read answer
	case "$answer" in
		[Yy]) ;;
		*)
			log "aborting"
			exit 0
			;;
	esac
fi

deploy
commit

log "done"

# __EOF__

