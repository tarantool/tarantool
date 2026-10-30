#!/bin/sh

set -e

APP_DIR="/opt/tarantool"
DATA_DIR="/var/lib/tarantool/sys_env"
LOG_DIR="/var/log/tarantool/sys_env"
RUN_DIR="/var/run/tarantool/sys_env"

DEFAULT_CONFIG="/default-config.yaml"
CUSTOM_CONFIG="${APP_DIR}/${TT_APP_NAME}/config.yaml"

if [ -f "${CUSTOM_CONFIG}" ] && [ -z "${TT_CONFIG}" ]; then
    export TT_CONFIG="${CUSTOM_CONFIG}"
else
    export TT_CONFIG="${TT_CONFIG:-${DEFAULT_CONFIG}}"
fi

export TT_SNAPSHOT_DIR_DEFAULT="${DATA_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}"
export TT_VINYL_DIR_DEFAULT="${DATA_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}"
export TT_WAL_DIR_DEFAULT="${DATA_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}"
export TT_LOG_FILE_DEFAULT="${LOG_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}/tarantool.log"
export TT_CONSOLE_SOCKET_DEFAULT="${RUN_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}/tarantool.control"
export TT_PROCESS_PID_FILE_DEFAULT="${RUN_DIR}/${TT_APP_NAME}/${TT_INSTANCE_NAME}/tarantool.pid"

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
