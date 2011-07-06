add_custom_target(test
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var
    )

tarantool_client("box/protocol" ${CMAKE_SOURCE_DIR}/test/box/protocol.c)
tarantool_client("box/connector" ${CMAKE_SOURCE_DIR}/test/box/connector.c)

install (PROGRAMS tarantool DESTINATION bin)
install (DIRECTORY lib DESTINATION bin)
install (FILES box/tarantool.cfg box/00000000000000000001.snap
    DESTINATION bin)
