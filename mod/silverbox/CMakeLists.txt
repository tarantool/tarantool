add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 mod/silverbox/memcached.rl -o mod/silverbox/memcached.c
    DEPENDS ${CMAKE_SOURCE_DIR}/mod/silverbox/memcached.rl)
# Do not clean memcached.c in 'make clean'.
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM 1)

set_source_files_properties(memcached.c
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

tarantool_module("silverbox" index.c box.c memcached.c)
