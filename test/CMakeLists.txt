add_custom_target(test
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --bindir=${PROJECT_BINARY_DIR}/mod/box --vardir=${PROJECT_BINARY_DIR}/test/var
    )

install (PROGRAMS tarantool DESTINATION bin)
install (DIRECTORY lib DESTINATION bin)
install (FILES box/tarantool.cfg box/00000000000000000001.snap
    DESTINATION bin)
