add_custom_target(test
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var
    )

tarantool_client("box/protocol" ${CMAKE_SOURCE_DIR}/test/box/protocol.c)
tarantool_client("connector_c/tt" ${CMAKE_SOURCE_DIR}/test/connector_c/tt.c)

install (FILES ${CMAKE_SOURCE_DIR}/test/box/tarantool.cfg
        DESTINATION bin)
install (FILES ${CMAKE_SOURCE_DIR}/test/box/00000000000000000001.snap
        DESTINATION bin)
