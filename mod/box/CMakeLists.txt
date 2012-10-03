if (TARGET_OS_DARWIN)
    set(module_link_flags "-pagezero_size 10000 -image_base 100000000")
endif()

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.m
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 mod/box/memcached-grammar.rl
                     -o mod/box/memcached-grammar.m
    DEPENDS ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.rl)
# Do not clean memcached-grammar.m in 'make clean'.
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM true)

add_custom_target(generate_memcached_grammar_m DEPENDS
    ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.m)

# Do not try to randomly re-generate memcached-grammar.m
# after a fresh checkout/branch switch.
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/mod/box/memcached-grammar.m)

function(lua_source filename)
    get_filename_component(module ${filename} NAME_WE)
    ADD_CUSTOM_COMMAND(OUTPUT ${CMAKE_BINARY_DIR}/mod/box/${filename}.c
        COMMAND ${ECHO} 'const char ${module}_lua[] =' > ${filename}.new.c
        COMMAND ${CMAKE_BINARY_DIR}/extra/txt2c
            ${CMAKE_SOURCE_DIR}/mod/box/${filename} >> ${filename}.new.c
        COMMAND ${ECHO} '\;' >> ${filename}.new.c
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${filename}.new.c
            ${CMAKE_BINARY_DIR}/mod/box/${filename}.c
        COMMAND ${CMAKE_COMMAND} -E remove ${filename}.new.c
        DEPENDS ${CMAKE_SOURCE_DIR}/mod/box/${filename})

    set(lua_sources ${lua_sources} ${CMAKE_BINARY_DIR}/mod/box/${filename}.c
        PARENT_SCOPE)
endfunction()

lua_source(box.lua)

add_custom_target(generate_lua_sources}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/mod/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

set_source_files_properties(memcached-grammar.m
    PROPERTIES HEADER_FILE_ONLY true)

set_source_files_properties(memcached.m
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

tarantool_module("box" tuple.m index.m tree.m space.m port.m request.m
    txn.m box.m box.lua.c queue.lua.c box_lua.m memcached.m memcached-grammar.m
    box_lua_uuid.m)
