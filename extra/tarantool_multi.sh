#!/bin/sh -x

# A wrapper script to run a single tarantool instance
# and restart it when it crashes

export PATH=$PATH:/usr/local/bin

NAME="tarantool_box"
BINARY="/usr/local/bin/${NAME}"
INST=$(basename $0 .sh)
CONF="/usr/local/etc/${INST}.cfg"
LOGDIR="/var/${INST}/logs"
WRAP_PIDFILE="/var/${INST}/wrapper.pid"
# set to get restart emails
#MAILTO=""

exec <&-

report()
{
        if [ "${MAILTO}" ]; then
                tail -n 500 ${LOGDIR}/tarantool.log | mail ${MAILTO} -s "\"${INST} is restarted\""
        fi
        echo \""${@}"\" >> ${LOGDIR}/wrapper.log
}

runtarantool()
{
        ulimit -n 40960
        ${BINARY} ${OPTIONS} --config ${CONF} 2>&1 </dev/null &
        wait
        RC=${?}
        report "${INST}: ${BINARY} ${OPTIONS} --config ${CONF} died prematurely "`date '+%Y-%m-%d %H:%M:%S'`" exit code $RC"
        sleep 2
}

{
        ulimit -c unlimited
        runtarantool

        while true
        do
                ulimit -c 0
                runtarantool
        done
} &

echo $! > ${WRAP_PIDFILE}
