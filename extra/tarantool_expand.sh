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

act_prompt=1
act_status=0
act_debug=0
act_dry=0

error() {
	echo "$prompt_name error: $*" 1>&2
	exit 1
}

log() {
	echo "$prompt_name > $*"
}

usage() {
	echo "Tarantool DB expand script"
	echo "usage: tarantool_expand.sh <options> <instances>"
	echo
	echo "  --prefix <path>       installation path ($prefix)"
	echo "  --prefix_etc <path>   installation etc path ($prefix_etc)"
	echo "  --prefix_var <path>   installation var path ($prefix_var)"
	echo
	echo "  --status              display deployment status"
	echo "  --dry                 don't create anything, show commands"
	echo
	echo "  --debug               show commands"
	echo "  --yes                 don't prompt"
	echo "  --help                display this usage"
	echo
	exit 0
}

rollback_instance() {
	id=$1
	workdir="${prefix_var}/tarantool$id"
	config="${prefix}/etc/tarantool$id.cfg"
	rm -rf $workdir
	rm -f $config
	rm -f "${prefix}/bin/tarantool$id.sh"
	rm -f "${prefix_etc}/init.d/tarantool$id"
}

rollback() {
	log ">>>> rollbacking changes"
	start=`expr $deploy_current + 1`
	for instance in `seq $start $deploy_count`; do
		rollback_instance $instance
	done
	exit 1
}

try() {
	cmd="$*"
	[ $act_debug -gt 0 ] && log $cmd
	if [ $act_dry -eq 0 ]; then 
		eval $cmd
		if [ $? -gt 0 ]; then
			rollback
		fi
	fi
}

deploy_instance() {
	id=$1
	workdir="${prefix_var}/tarantool$id"
	config="${prefix}/etc/tarantool$id.cfg"

	log ">>>> deploying instance $id"

	# setting up work environment
	try "mkdir -p $workdir/logs"
	# setting up startup snapshot
	try "cp \"${prefix}/share/tarantool/00000000000000000001.snap\" $workdir"
	try "chown tarantool:tarantool -R $workdir"

	# setting up configuration file
	try "cp \"${prefix}/etc/tarantool.cfg\" $config"
	try "echo \"work_dir = \"$workdir\"\" >> $config"
	try "echo \"username = \"tarantool\"\" >> $config"
	try "echo \"logger = \"cat - \>\> logs/tarantool.log\"\" >> $config"

	# setting up wrapper
	try "ln -s \"${prefix}/bin/tarantool_multi.sh\" \"${prefix}/bin/tarantool$id.sh\""
	
	# setting up startup script
	try "ln -s \"${prefix_etc}/init.d/tarantool\" \"${prefix_etc}/init.d/tarantool$id\""
}

deploy() {
	start=`expr $deploy_current + 1`
	for instance in `seq $start $deploy_count`; do
		deploy_instance $instance
	done
}

commit() {
	log ">>>> updating deploy config"
	try "echo $deploy_count > $deploy_cfg"
}

# processing command line arguments
if [ $# -eq 0 ]; then
	usage
fi
while [ $# -ge 1 ]; do
	case $1 in
		--yes) act_prompt=0 ; shift 1 ;;
		--prefix) prefix=$2 ; shift 2 ;;
		--prefix_var) prefix_var=$2 ; shift 2 ;;
		--prefix_etc) prefix_etc=$2 ; shift 2 ;;
		--dry) act_dry=1 ; act_debug=1 ; shift 1;;
		--debug) act_debug=1; shift 1;;
		--status) act_status=1 ; shift 1 ;;
		--help) usage ; shift 1 ;;
		*) deploy_count=$1 ; shift 1 ; break ;;
	esac
done

# checking deployment configuration file
if [ -f $deploy_cfg ]; then
	deploy_exists=1
	deploy_current=`cat $deploy_cfg`
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
[ $deploy_count -eq $deploy_count ] 2>/dev/null && [ $deploy_count -gt 0 ] || \
	error "bad instance number"

if [ $deploy_count -le $deploy_current ]; then
	error "expand only is supported (required instances number $deploy_count" \
	      "is lower/equal than deployed $deploy_current)" 
fi

# asking permission to continue
if [ $act_prompt -eq 1 ]; then
	[ $act_dry -ne 0 ] && log "(dry mode)"
	log "About to extend tarantool instances from $deploy_current to $deploy_count."
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
commit

log "done"
