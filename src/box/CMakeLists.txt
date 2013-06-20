if (TARGET_OS_DARWIN)
    set(module_link_flags "-pagezero_size 10000 -image_base 100000000")
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)

set(lua_sources)
lua_source(lua_sources lua/box.lua)
lua_source(lua_sources lua/box_net.lua)
lua_source(lua_sources lua/misc.lua)
lua_source(lua_sources lua/sql.lua)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

tarantool_module("box" tuple.cc index.cc hash_index.cc tree_index.cc space.cc
    port.cc request.cc tuple_update.cc txn.cc box.cc ${lua_sources} box_lua.cc box_lua_space.cc
    bitset_index.cc)

target_link_libraries(tarantool_box bitset)
