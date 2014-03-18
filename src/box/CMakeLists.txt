if (TARGET_OS_DARWIN)
    set(module_link_flags "-pagezero_size 10000 -image_base 100000000")
endif()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)

set(lua_sources)
lua_source(lua_sources lua/schema.lua)
lua_source(lua_sources lua/box.lua)
lua_source(lua_sources lua/box_net.lua)
lua_source(lua_sources lua/misc.lua)
lua_source(lua_sources lua/tuple.lua)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

tarantool_module("box"
    tuple.cc
    tuple_convert.cc
    tuple_update.cc
    key_def.cc
    index.cc
    hash_index.cc
    tree_index.cc
    bitset_index.cc
    sophia_index.cc
    space.cc
    alter.cc
    schema.cc
    port.cc
    request.cc
    txn.cc
    box.cc
    ${lua_sources}
    lua/call.cc
    lua/tuple.cc
    lua/slab.cc
    lua/index.cc
    lua/space.cc)

include_directories(${SOPHIA_INCLUDE_DIR})
set (sophia_lib "${PROJECT_BINARY_DIR}/third_party/sophia/db/libsophia.a")

target_link_libraries(tarantool_box bitset msgpuck ${sophia_lib} -rdynamic)
