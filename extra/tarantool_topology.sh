#!/bin/sh

#
# Tarantool DB topology setup script
#

topology_cfg='/usr/local/etc/tarantool_topology.cfg'
topology_exists=0
topology_count=0
prompt=1
ts=`/bin/date +"%Y%m%d-%H%M%S"`

if [ -f $topology_cfg ]; then
        topology_exists=1
        topology_count=`cat $topology_cfg`
        # dont' change topology if it said so in configuration file
        if [ $topology_count -eq 0 ]; then
                echo "skipping topology setup"
                exit 0
        fi
fi

usage() {
        echo "Tarantool DB topology setup script"
        echo
        echo "usage: tarantool_topology.sh [-y] <servers>"
        exit 0
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
isnum=0
if [ $num -eq $num 2> /dev/null ]; then
        isnum=1
fi
if [ $isnum -eq 0 ] || [ $num -eq 0 2> /dev/null ]; then
        echo bad instance number
        exit 1
fi

# asking permission to continue
#
if [ $prompt -eq 1 ]; then
        echo "About to create new Tarantool DB topology for $num instances."
        if [ $topology_exists -eq 1 ]; then
                echo "Old data and configuration will be saved with $ts time-stamp prefix."
        fi
        echo "Are you sure? [n/y]"
        read answer
        case "$answer" in
                [Yy]) ;;
                *)
			echo "aborting"
			exit 0
                        ;;
        esac
fi

backup_instance() {
        id=$1
	workdir="/var/tarantool$id"
	workdir_new="/var/tarantool${id}_${ts}"
	config="/usr/local/etc/tarantool$id.cfg"
	config_new="/usr/local/etc/tarantool${id}-${ts}.cfg"

        echo ">> making instance $id backup"

	# moving old instance directory
	mv $workdir $workdir_new

	# moving old configuration file
	mv $config $config_new

	# removving startup and wrapper links
	rm -f "/etc/init.d/tarantool$id"
	rm -f "/usr/local/bin/tarantool$id.sh"
}

backup() {
        echo "making backup for $topology_count instances"
        to=$(( $topology_count - 1 ))
        for instance in `seq 0 $to`; do
                backup_instance $instance
        done
}
if [ $topology_exists -eq 1 ]; then
        backup
fi

topology_count=$num

deploy_instance() {
	id=$1
	workdir="/var/tarantool$id"
	config="/usr/local/etc/tarantool$id.cfg"

        echo ">> deploying instance $id"

	# setting up work environment
	mkdir $workdir
	mkdir $workdir/logs

	chown tarantool:tarantool -R $workdir

	# setting up startup snapshot
	cp "/usr/local/share/tarantool/00000000000000000001.snap" $workdir

	# setting up configuration file
	cp "/usr/local/etc/tarantool.cfg" $config

	echo "work_dir = \"$workdir\"" >> $config
	echo "username = \"tarantool\"" >> $config
	echo "logger = \"cat - >> logs/tarantool.log\"" >> $config

	# setting up wrapper
	ln -s "/usr/local/bin/tarantool_multi.sh" "/usr/local/bin/tarantool$id.sh"
	
	# setting up startup script
	ln -s "/etc/init.d/tarantool" "/etc/init.d/tarantool$id"
}

deploy() {
        to=$(( $topology_count - 1 ))
        for instance in `seq 0 $to`; do
                deploy_instance $instance
        done
}

update() {
        echo "updating topology config"
        echo $topology_count > $topology_cfg
}

deploy
update

echo done
