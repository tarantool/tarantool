if (TARGET_OS_DARWIN)
    set(module_link_flags "-pagezero_size 10000 -image_base 100000000")
endif()

function(lua_source filename)
    get_filename_component(module ${filename} NAME_WE)
    ADD_CUSTOM_COMMAND(OUTPUT ${CMAKE_BINARY_DIR}/src/box/${filename}.c
        COMMAND ${ECHO} 'const char ${module}_lua[] =' > ${filename}.new.c
        COMMAND ${CMAKE_BINARY_DIR}/extra/txt2c
            ${CMAKE_SOURCE_DIR}/src/box/${filename} >> ${filename}.new.c
        COMMAND ${ECHO} '\;' >> ${filename}.new.c
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${filename}.new.c
            ${CMAKE_BINARY_DIR}/src/box/${filename}.c
        COMMAND ${CMAKE_COMMAND} -E remove ${filename}.new.c
        DEPENDS ${CMAKE_SOURCE_DIR}/src/box/${filename} txt2c)

    set(lua_sources ${lua_sources} ${CMAKE_BINARY_DIR}/src/box/${filename}.c
        PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)
lua_source(lua/box.lua)
lua_source(lua/box_net.lua)
lua_source(lua/misc.lua)

add_custom_target(generate_lua_sources}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

tarantool_module("box" tuple.m index.m hash_index.m tree_index.m space.m
    port.m request.m txn.m box.m ${lua_sources} box_lua.m box_lua_space.m
    bitset_index.m)

target_link_libraries(tarantool_box bitset)
