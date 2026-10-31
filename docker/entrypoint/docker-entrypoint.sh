#!/bin/sh

set -e

# Handle args such as `-v` or `--help`.
if [ " -${1#?}" = " $1" ]; then
    set -- tarantool "$@"
fi

# Only inspect options that can precede a script. Tarantool owns validation;
# unknown options and non-server actions receive the user's environment only.
is_server() {
    [ "$1" = "tarantool" ] || return 1
    shift
    while [ "$#" -gt 0 ]; do
        case "$1" in
            # After --, any remaining argument is a script.
            --)
                shift
                [ "$#" -eq 0 ]
                return
                ;;
            --*)
                # Match --config=file.yaml just like --config file.yaml.
                option="${1%%=*}"
                case "${option}" in
                    # Tarantool also accepts prefixes such as --conf.
                    --c|--co|--con|--conf|--confi|--config|\
                    --n|--na|--nam|--name) ;;
                    --f?*)
                        # Accept --force-recovery prefixes, not --failover.
                        case --force-recovery in
                            "${option}"*) ;;
                            *) return 1 ;;
                        esac
                        # --force-recovery takes no value.
                        [ "$1" = "${1%%=*}" ] || return 1
                        shift
                        continue ;;
                    # Help, version, and unknown options need no defaults.
                    *) return 1 ;;
                esac
                if [ "$1" = "${1%%=*}" ]; then
                    # A separate config/name value is not a script.
                    shift
                    [ "$#" -gt 0 ] || return 1
                fi
                ;;
            -?*)
                # Combined flags like -iljson need letter-by-letter parsing.
                short_options="${1#-}"
                shift
                while [ -n "${short_options}" ]; do
                    option="${short_options%"${short_options#?}"}"
                    short_options="${short_options#?}"
                    case "${option}" in
                        # -i and -d alone still allow server defaults.
                        i|d) ;;
                        c|n|l|j)
                            # Values can be attached (-ljson) or next (-l json).
                            if [ -z "${short_options}" ]; then
                                [ "$#" -gt 0 ] || return 1
                                shift
                            fi
                            short_options=""
                            ;;
                        # -e, -b, help/version, or unknown: no server defaults.
                        *) return 1 ;;
                    esac
                done
                continue
                ;;
            # A script or stdin (-) overrides earlier server flags.
            *) return 1 ;;
        esac
        shift
    done
    return 0
}

if is_server "$@"; then
    # Unlike ':-', '-' preserves explicit empty values, even on re-execution.
    export TT_APP_NAME="${TT_APP_NAME-default}"
    export TT_INSTANCE_NAME="${TT_INSTANCE_NAME-instance-001}"

    APP_DIR="/opt/tarantool"
    INSTANCE_DIR="${TT_APP_NAME}/${TT_INSTANCE_NAME}"
    DATA_DIR="/var/lib/tarantool/sys_env/${INSTANCE_DIR}"
    LOG_DIR="/var/log/tarantool/sys_env/${INSTANCE_DIR}"
    RUN_DIR="/var/run/tarantool/sys_env/${INSTANCE_DIR}"

    DEFAULT_CONFIG="/default-config.yaml"
    CUSTOM_CONFIG="${APP_DIR}/${TT_APP_NAME}/config.yaml"

    if [ -f "${CUSTOM_CONFIG}" ] && [ -z "${TT_CONFIG}" ]; then
        export TT_CONFIG="${CUSTOM_CONFIG}"
    else
        export TT_CONFIG="${TT_CONFIG:-${DEFAULT_CONFIG}}"
    fi

    export TT_SNAPSHOT_DIR_DEFAULT="${TT_SNAPSHOT_DIR_DEFAULT-${DATA_DIR}}"
    export TT_VINYL_DIR_DEFAULT="${TT_VINYL_DIR_DEFAULT-${DATA_DIR}}"
    export TT_WAL_DIR_DEFAULT="${TT_WAL_DIR_DEFAULT-${DATA_DIR}}"
    export TT_LOG_FILE_DEFAULT="${TT_LOG_FILE_DEFAULT-${LOG_DIR}/tarantool.log}"
    SOCKET="${RUN_DIR}/tarantool.control"
    PID_FILE="${RUN_DIR}/tarantool.pid"
    export TT_CONSOLE_SOCKET_DEFAULT="${TT_CONSOLE_SOCKET_DEFAULT-${SOCKET}}"
    TT_PROCESS_PID_FILE_DEFAULT="${TT_PROCESS_PID_FILE_DEFAULT-${PID_FILE}}"
    export TT_PROCESS_PID_FILE_DEFAULT
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
