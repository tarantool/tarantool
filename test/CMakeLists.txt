add_custom_target(test
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var
    )

add_subdirectory(box)
add_subdirectory(connector_c)

install (FILES ${CMAKE_SOURCE_DIR}/test/box/tarantool.cfg
        DESTINATION etc)
install (FILES ${CMAKE_SOURCE_DIR}/test/box/00000000000000000001.snap
        DESTINATION var/lib/tarantool)
