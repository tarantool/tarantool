add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.m
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 mod/silverbox/memcached.rl -o mod/silverbox/memcached.m
    DEPENDS ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.rl)
# Do not clean memcached.m in 'make clean'.
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM 1)

set_source_files_properties(memcached.m
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

tarantool_module("box" index.m box.m memcached.m)
