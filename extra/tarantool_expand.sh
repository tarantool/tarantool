#!/bin/sh

#
# Tarantool DB expand script
#

prefix="/usr/local"
prefix_var="/var"
prefix_etc="/etc"

deploy_cfg="${prefix}/etc/tarantool_deploy.cfg"
deploy_exists=0
deploy_current=0
deploy_count=0

prompt_name=`basename $0`
prompt=1

error() {
	echo "$prompt_name error: $*" 1>&2
	exit 1
}

log() {
	echo "$prompt_name > $*"
}

usage() {
	echo "Tarantool DB expand script"
	echo
	echo "usage: tarantool_expand.sh [-y] <servers>"
	exit 0
}

rollback_instance() {
	id=$1
	workdir="${prefix_var}/tarantool$id"
	config="${prefix}/etc/tarantool$id.cfg"

	rm -f "${prefix}/bin/tarantool$id.sh"
	rm -f "${prefix_etc}/init.d/tarantool$id"
	rm -f $config
	rm -rf $work_dir
}

rollback() {
	log "rollbacking changes"
	for instance in `seq $deploy_current $deploy_count`; do
		rollback_instance $instance
	done
	exit 1
}

try() {
	cmd="$*"
	log $cmd
	eval $cmd
	if [ $? -gt 0 ]; then
#		log "failed: $cmd"
		rollback
	fi
}

deploy_instance() {
	id=$1
	workdir="${prefix_var}/tarantool$id"
	config="${prefix}/etc/tarantool$id.cfg"

	log "-- deploying instance $id"

	# setting up work environment
	try "mkdir -p $workdir/logs"
	try "chown tarantool:tarantool -R $workdir"

	# setting up startup snapshot
	try "cp \"${prefix}/share/tarantool/00000000000000000001.snap\" $workdir"

	# setting up configuration file
	try "cp \"${prefix}/etc/tarantool.cfg\" $config"

	try "echo \"work_dir = \"$workdir\"\" >> $config"
	try "echo \"username = \"tarantool\"\" >> $config"
	try "echo \"logger = \"cat - >> logs/tarantool.log\"\" >> $config"

	# setting up wrapper
	try "ln -s \"${prefix}/bin/tarantool_multi.sh\" \"${prefix}/bin/tarantool$id.sh\""
	
	# setting up startup script
	try "ln -s \"${prefix_etc}/init.d/tarantool\" \"${prefix_etc}/init.d/tarantool$id\""
}

deploy() {
	for instance in `seq $deploy_current $deploy_count`; do
		deploy_instance $instance
	done
}

update() {
	log "-- updating deploy config"
	try "echo $deploy_current > $deploy_cfg"
}

# processing command line arguments
if [ $# -eq 2 ]; then
	if [ "$1" != "-y" ]; then
		usage
	fi
	prompt=0
	deploy_count=$2
else
	if [ $# -ne 1 ]; then
		usage
	fi
	deploy_count=$1
fi

# validating instance number
[ $deploy_count -eq $deploy_count -o $deploy_count -le 0 ] 2>/dev/null || \
	error "bad instance number"

if [ -f $deploy_cfg ]; then
	deploy_exists=1
	deploy_current=`cat $deploy_cfg`
	# dont' change deploy if it said so in configuration file
	if [ $deploy_current -eq 0 ]; then
		log "skipping deploy setup"
		exit 0
	fi
fi

if [ $deploy_count -le $deploy_current ]; then
	error "expand only is supported (required instances number $deploy_count" \
	      "is lower/equal than deployed $deploy_current)" 
fi

# asking permission to continue
if [ $prompt -eq 1 ]; then
	log "About to deploy $deploy_current - $deploy_count tarantool instances."
	log "Run? [n/y]"
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
update

log "done"
