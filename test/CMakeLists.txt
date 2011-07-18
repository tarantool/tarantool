add_custom_target(test
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var
    )

add_subdirectory(box)

install (PROGRAMS tarantool DESTINATION bin)
install (DIRECTORY lib DESTINATION bin)
install (FILES ${CMAKE_SOURCE_DIR}/box/tarantool.cfg
        ${CMAKE_SOURCE_DIR}/box/00000000000000000001.snap
        DESTINATION bin)
