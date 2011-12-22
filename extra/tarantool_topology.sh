#!/bin/sh

##
## Tarantool DB topology setup script
##

prefix="/usr/local"
prefix_var="/var"
prefix_etc="/etc"

topology_cfg="${prefix}/etc/tarantool_topology.cfg"
topology_exists=0
topology_count=0
prompt=1
name=`basename $0`

error() {
	echo "$name error: $*" 1>&2
	exit 1
}

log() {
	echo "$name > $*"
}

toolchain_check() {
	for tool in $*; do
		which $tool 1>/dev/null 2>&1
		if [ $? -gt 0 ]; then
			error "required utility '$tool' is not found" 
		fi
	done
}

usage() {
	echo "Tarantool DB topology setup script"
	echo
	echo "usage: tarantool_topology.sh [-y] <servers>"
	exit 0
}

backup_instance() {
	id=$1
	workdir="${prefix_var}/tarantool$id"
	workdir_new="${prefix_var}/tarantool${id}_${ts}"
	config="${prefix}/etc/tarantool$id.cfg"
	config_new="${prefix}/etc/tarantool${id}-${ts}.cfg"

	log "making instance $id backup"

	# moving old instance directory
	mv $workdir $workdir_new

	# moving old configuration file
	mv $config $config_new

	# removing startup and wrapper links
	rm -f "${prefix_etc}/init.d/tarantool$id"
	rm -f "${prefix}/bin/tarantool$id.sh"
}

backup() {
	log "making backup for $topology_count instances"
	to=`expr $topology_count - 1`
	for instance in `seq 0 $to`; do
		backup_instance $instance
	done
}

deploy_instance() {
	id=$1
	workdir="${prefix_var}/tarantool$id"
	config="${prefix}/etc/tarantool$id.cfg"

	log "deploying instance $id"

	# setting up work environment
	mkdir -p $workdir/logs

	chown tarantool:tarantool -R $workdir

	# setting up startup snapshot
	cp "${prefix}/share/tarantool/00000000000000000001.snap" $workdir

	# setting up configuration file
	cp "${prefix}/etc/tarantool.cfg" $config

	echo "work_dir = \"$workdir\"" >> $config
	echo "username = \"tarantool\"" >> $config
	echo "logger = \"cat - >> logs/tarantool.log\"" >> $config

	# setting up wrapper
	ln -s "${prefix}/bin/tarantool_multi.sh" "/usr/local/bin/tarantool$id.sh"
	
	# setting up startup script
	ln -s "${prefix_etc}/init.d/tarantool" "${prefix_etc}/init.d/tarantool$id"
}

deploy() {
	to=`expr $topology_count - 1`
	for instance in `seq 0 $to`; do
		deploy_instance $instance
	done
}

update() {
	log "updating topology config"
	echo $topology_count > $topology_cfg
}

# processing command line arguments
#
num=0
if [ $# -eq 2 ]; then
	if [ "$1" != "-y" ]; then
		usage
	fi
	prompt=0
	num=$2
else
	if [ $# -ne 1 ]; then
		usage
	fi
	num=$1
fi

# validating instance number
#
[ $num -eq $num -o $num -le 0 ] 2>/dev/null || error "bad instance number"

if [ -f $topology_cfg ]; then
	topology_exists=1
	topology_count=`cat $topology_cfg`
	# dont' change topology if it said so in configuration file
	if [ $topology_count -eq 0 ]; then
		log "skipping topology setup"
		exit 0
	fi
fi

toolchain_check "date" "expr"

# time-stamp
#
ts=`/bin/date +"%Y%m%d-%H%M%S"`

# asking permission to continue
#
if [ $prompt -eq 1 ]; then
	log "About to create new Tarantool DB topology for $num instances."
	if [ $topology_exists -eq 1 ]; then
		log "Old data and configuration will be saved with $ts time-stamp prefix."
	fi
	log "Are you sure? [n/y]"
	read answer
	case "$answer" in
		[Yy]) ;;
		*)
			log "aborting"
			exit 0
			;;
	esac
fi

# stop on error
#
set -e

if [ $topology_exists -eq 1 ]; then
	backup
fi

# updating instances count
#
topology_count=$num

deploy
update

log "done"
