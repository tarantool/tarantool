enable_testing()

add_test(
    NAME check-dependencies
    COMMAND ${CMAKE_COMMAND}
        -D FILE=${TARANTOOL_BINARY}
        -P ${CMAKE_CURRENT_LIST_DIR}/CheckDependencies.cmake
)
