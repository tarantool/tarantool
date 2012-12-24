
include_directories("${PROJECT_SOURCE_DIR}/connector/c/include")

tarantool_client("protocol" protocol.c)
