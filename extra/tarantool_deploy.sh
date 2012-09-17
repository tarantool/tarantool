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
# Tarantool instance deployment script
#

prompt_name=`basename $0`

act_prompt=1
act_status=0
act_debug=0
act_dry=0

error() {
	echo "$prompt_name error: $*" 1>&2
	exit 1
}

log() {
	echo "$prompt_name: $*"
}

usage() {
	echo "Tarantool deployment script: add more Tarantool instances."
	echo "usage: tarantool_deploy.sh [options] <instance>"
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
	log ">>> rollback changes"
	rollback_instance $deploy_name
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

deploy() {
	id=$1
	workdir="${prefix_var}/tarantool_box$id"
	config="${prefix}/etc/tarantool_box$id.cfg"

	log ">>> deploy instance $id"

	# setup work environment
	try "mkdir -p $workdir/logs"

	# setup startup snapshot
	try "cp \"${prefix}/share/tarantool/00000000000000000001.snap\" $workdir"
	try "chown tarantool:tarantool -R $workdir"

	# setup configuration file
	try "cp \"${prefix}/etc/tarantool.cfg\" $config"
	try 'echo work_dir = \"$workdir\" >> $config'
	try 'echo username = \"tarantool\" >> $config'
	try 'echo logger = \"cat - \>\> logs/tarantool.log\" >> $config'

	# setup wrapper
	try "ln -s \"${prefix}/bin/tarantool_multi.sh\" \"${prefix}/bin/tarantool_box$id.sh\""

	# setup startup script
	try "ln -s \"${prefix_etc}/init.d/tarantool_box\" \"${prefix_etc}/init.d/tarantool_box$id\""
}

deploy_check() {
	id=$1
	# check, if instance is already exist (configuration file, consistent way)
	if [ $deploy_exists -eq 1 ]; then
		grep "^\(${id}\)$" $deploy_cfg > /dev/null
		if [ $? -eq 0 ]; then
			log "Instance '${id}' is already deployed."
			exit 0
		fi
	fi
	# check, if there are any instance-related files exists that could be
	# accidently removed or overwritten by setup.
	instance_workdir="${prefix_var}/tarantool_box$id"
	instance_config="${prefix}/etc/tarantool_box$id.cfg"
	instance_wrapper="${prefix}/bin/tarantool_box$id.sh"
	isntance_startup="${prefix_etc}/init.d/tarantool_box$id"
	[ -d $instance_workdir ] && error "Instance workdir exists: '$instance_workdir'"
	[ -f $instance_config ] && error "Instance configuration file exists: $instance_config"
	[ -f $instance_wrapper ] && error "Instance wrapper file exists: $instance_wrapper"
	[ -f $instance_startup ] && error "Instance startup file exists: $instance_startup"
}

commit() {
	log ">>> updating deployment config"
	try "echo $1 >> $deploy_cfg"
}

# process command line arguments
[ $# -eq 0 ] && usage 1

deploy_name_set=0
deploy_name=""
while [ $# -ge 1 ]; do
	case $1 in
		--yes) act_prompt=0; shift 1 ;;
		--prefix) prefix=$2; shift 2 ;;
		--prefix_var) prefix_var=$2; shift 2 ;;
		--prefix_etc) prefix_etc=$2; shift 2 ;;
		--dry) act_dry=1 ; act_debug=1 ; shift 1 ;;
		--debug) act_debug=1; shift 1 ;;
		--status) act_status=1; shift 1 ;;
		--help) usage 0; shift 1 ;;
		*) deploy_name=$1; deploy_name_set=1; break ;;
	esac
done

set ${prefix:="/usr/local"}
set ${prefix_var:="/var"}
set ${prefix_etc:="/etc"}

deploy_cfg="${prefix}/etc/tarantool_deploy.cfg"
deploy_exists=0

# check deployment configuration file
[ -f $deploy_cfg ] && deploy_exists=1

# display status
if [ $act_status -ne 0 ]; then
	if [ $deploy_exists -eq 0 ]; then
		log "No tarantool instances found."
	else
		log "Current instances:\n`cat $deploy_cfg`"
	fi
	exit 0
fi

# check that instance name was specified
[ $deploy_name_set -eq 0 ] && usage 1

# validate instance name
echo $deploy_name | grep '^[[:digit:]]\+.\(1\|2\)' > /dev/null
if [ $? -eq 1 ]; then 
	error "Bad instance name format, should be e.g: 1.1, 1.2, etc."
fi

# check if it consistent to deploy new instance
deploy_check $deploy_name

# ask permission to continue
if [ $act_prompt -eq 1 ]; then
	[ $act_dry -ne 0 ] && log "(dry mode)"
	log "About to deploy Tarantool instance $deploy_name."
	log "Continue? [n/y]"
	read answer
	case "$answer" in
		[Yy]) ;;
		*)
			log "Abort"
			exit 0
			;;
	esac
fi

deploy $deploy_name
commit $deploy_name

log "done"

# __EOF__

