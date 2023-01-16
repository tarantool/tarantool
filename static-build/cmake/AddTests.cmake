enable_testing()

add_test(
    NAME check-dependencies
    COMMAND ${CMAKE_COMMAND}
        -D FILE=${TARANTOOL_BINARY}
        -P ${CMAKE_CURRENT_LIST_DIR}/CheckDependencies.cmake
)

add_test(
    NAME check-exports
    COMMAND ${TARANTOOL_BINARY}
            ${CMAKE_CURRENT_LIST_DIR}/../test/exports.test.lua
)

add_test(
    NAME check-traceback
    COMMAND ${TARANTOOL_BINARY}
            ${CMAKE_CURRENT_LIST_DIR}/../test/traceback.test.lua
)

add_test(
    NAME check-luarocks
    COMMAND ${TARANTOOL_BINARY}
            ${CMAKE_CURRENT_LIST_DIR}/../test/luarocks.test.lua
)
