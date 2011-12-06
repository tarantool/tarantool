#!/bin/sh -x

export PATH=$PATH:/usr/local/bin

NAME="tarantool_box"
BINARY="/usr/local/bin/${NAME}"
INST=$(basename $0 .sh)
CONF="/usr/local/etc/${INST}.cfg"
LOGDIR="/var/${INST}/logs"
WRAP_PIDFILE="/var/${INST}/wrapper.pid"

exec <&-

report()
{
        tail -n 500 ${LOGDIR}/tarantool.log | mail -s "${@}" ${MAILTO}
}

runtarantool()
{
        ulimit -n 40960
        ${BINARY} ${OPTIONS} --config ${CONF} 2>&1 </dev/null &
        wait
        RC=${?}
        report "${INST} restarted! "`date '+%Y-%m-%d %H:%M:%S'`" exit code $RC"
        echo "${INST} restarted! "`date '+%Y-%m-%d %H:%M:%S'`" exit code $RC<br>" >> /var/tmp/error.txt
        sleep 2
}

{
        ulimit -Hc unlimited
        runtarantool

        while true
        do
                ulimit -Hc 0
                runtarantool
        done
} &

echo $! > ${WRAP_PIDFILE}
