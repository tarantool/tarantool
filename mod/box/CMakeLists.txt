add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.m
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 mod/box/memcached-grammar.rl
                     -o mod/box/memcached-grammar.m
    DEPENDS ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.rl)
# Do not clean memcached-grammar.m in 'make clean'.
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM 1)

# Do not try to randomly re-generate memcached-grammar.m
# after a fresh checkout/branch switch.
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.m)

set_source_files_properties(memcached-grammar.m
    PROPERTIES HEADER_FILE_ONLY true)

set_source_files_properties(memcached.m
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

tarantool_module("box" index.m box.m box_lua.m memcached.m memcached-grammar.m)
