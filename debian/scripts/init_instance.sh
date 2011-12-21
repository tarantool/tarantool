#!/bin/sh

set -e

CFG=$1
ACTION=$2
CONFIG_DIR=/var/lib/tarantool/started
SNAPSHOT_DIR=/var/lib/tarantool/snapshot
PID_DIR=/var/run/tarantool
LOG_DIR=/var/log/tarantool
BOX=/usr/bin/tarantool_box
SSD=start-stop-daemon

usage="Usage: sh $0 /path/to/config.file start|stop"

if test -z "$CFG"; then
    echo $usage
    exit 5
fi

if ! echo $ACTION|grep -q '^\(start\|stop\)$'; then
    echo $usage
    exit 5
fi

if ! test -r "$CFG"; then
    echo File $CFG not found
    exit 10
fi


NAME=`basename $CFG .cfg`

PID=$PID_DIR/$NAME.pid
SCFG=$CONFIG_DIR/$NAME
RUNDIR=$SNAPSHOT_DIR/$NAME
LOG=$LOG_DIR/$NAME.log
SOCKETS=`grep \
    '^[[:space:]]*file_descriptors[[:space:]]*=[[:space:]]*[[:digit:]]\+' $CFG \
    | tail -n 1 \
    | sed 's/[^[:digit:]]//g'
`

SSDARGS_NO_PID="--quiet --chdir $RUNDIR --chuid tarantool --exec"
SSDARGS="--pidfile $PID $SSDARGS_NO_PID"

if [ $SOCKETS -gt 1024  -a $SOCKETS -lt 65000 ]; then
    ulimit -n $SOCKETS
fi

ulimit -c unlimited

comment_str="#### - commented by init script"
sed "s/^[[:space:]]*logger.*/# & $comment_str/" $CFG \
    | sed "s/^[[:space:]]*file_descriptors.*/# & $comment_str/" \
    | sed "s/^[[:space:]]*pid_file.*/# & $comment_str/" > $SCFG

$BOX -c $SCFG -v --check-config

echo "pid_file = $PID"              >> $SCFG
echo "logger   = \"cat >> $LOG\""   >> $SCFG

if [ ! -d $RUNDIR ]; then
    install -d -otarantool -gtarantool -m0750 $RUNDIR
    cd $RUNDIR
    if ! $SSD --start $SSDARGS $BOX -- --init-storage -v -c $SCFG;
    then
        rm -fr $RUNDIR
        exit 15
    fi
fi

if [ $ACTION = 'start' ]; then
    echo -n "\tStarting '$NAME' ... "
else
    echo -n "\tStopping '$NAME' ... "
fi

if $SSD --$ACTION $SSDARGS $BOX -- -B -v -c $SCFG >/dev/null 2>&1;
then
    echo "ok"
else
    ret=$?
    if [ $ret -eq 1 ]; then
        if [ $ACTION = 'start' ]; then
            echo "already started"
        else
            echo "already stoppped"
        fi
    else
        echo "failed"
    fi
fi
