#!/bin/bash

# Конфигурация
SERVER_CORES="0"       # Ядро для сервера
CLIENT_CORES="1"       # Ядро для клиента
SERVER_DIR="server"
CLIENT_DIR="client"
ENV_SUCCESSFUL_LOGS=1
WITHOUT_PROXY="true"
WORKING_LOG_PATH="./working_log.log"


# Проверка доступности утилит
check_requirements() {
    command -v taskset >/dev/null || { echo "Установите util-linux"; exit 1; }
    command -v tarantool >/dev/null 2>&1 || { echo >&2 "Ошибка: Tarantool не установлен"; exit 1; }
    command -v python3 >/dev/null 2>&1 || { echo >&2 "Ошибка: Python3 не установлен"; exit 1; }
    [ $(nproc) -gt 1 ] || { echo "Нужен многоядерный CPU"; exit 1; }
}

start_server() {
    echo "Запуск сервера на ядре $SERVER_CORES"
    (cd $SERVER_DIR && taskset -c $SERVER_CORES tarantool fifo_server.lua) &
    SERVER_PID=$!
}

start_client() {
    echo "Запуск клиента на ядре $CLIENT_CORES"
    (cd $CLIENT_DIR && taskset -c $CLIENT_CORES python3 main.py)
}

cleanup() {
    kill $SERVER_PID 2>/dev/null
    pkill -9 tarantool
}

# Main
cleanup
check_requirements
trap cleanup INT TERM
start_server
sleep 2
start_client
cleanup
exit 0