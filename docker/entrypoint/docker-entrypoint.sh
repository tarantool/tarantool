#!/bin/sh

set -e

APP_DIR="/opt/tarantool"
DATA_DIR="/var/lib/tarantool"
LOG_DIR="/var/log/tarantool"
RUN_DIR="/var/run/tarantool"

APP_CONFIG="${APP_DIR}/${TT_APP_NAME}/config.yaml"

if [ -f "${APP_CONFIG}" ] && [ -z "${TT_CONFIG}" ]; then
    export TT_CONFIG="${APP_CONFIG}"
else
    export TT_CONFIG="${TT_CONFIG:-/default-config.yaml}"
fi

export TT_SNAPSHOT_DIR="${TT_SNAPSHOT_DIR:-${DATA_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}}"
export TT_VINYL_DIR="${TT_VINYL_DIR:-${DATA_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}}"
export TT_WAL_DIR="${TT_WAL_DIR:-${DATA_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}}"
export TT_LOG_FILE="${TT_LOG_FILE:-${LOG_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}/tarantool.log}"
export TT_CONSOLE_SOCKET="${TT_CONSOLE_SOCKET:-${RUN_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}/tarantool.control}"
export TT_PROCESS_PID_FILE="${TT_PROCESS_PID_FILE:-${RUN_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}/tarantool.pid}"

# Handle args such as `-v` or `--help`.
if [ " -${1#?}" = " $1" ]; then
    set -- tarantool "$@"
fi

# Allow the container to be started without `--user tarantool`.
if [ "$1" = "tarantool" ] && [ "$(id -u)" = "0" ]; then
    exec gosu tarantool "$0" "$@"
fi

if [ "$1" = "tarantool" ]; then
    shift
    exec tarantool "$@"
fi

exec "$@"
