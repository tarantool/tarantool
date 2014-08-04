file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)

include_directories(${SOPHIA_INCLUDE_DIR})

set(lua_sources)
lua_source(lua_sources lua/load_cfg.lua)
lua_source(lua_sources lua/schema.lua)
lua_source(lua_sources lua/tuple.lua)
lua_source(lua_sources lua/session.lua)
set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

add_library(box
    tuple.cc
    tuple_convert.cc
    tuple_update.cc
    key_def.cc
    index.cc
    hash_index.cc
    tree_index.cc
    bitset_index.cc
    engine.cc
    engine_memtx.cc
    engine_sophia.cc
    sophia_index.cc
    space.cc
    alter.cc
    schema.cc
    session.cc
    port.cc
    request.cc
    txn.cc
    box.cc
    access.cc
    authentication.cc
    vclock.c
    cluster.cc
    log_io.cc
    recovery.cc
    replica.cc
    replication.cc
    ${lua_sources}
    lua/call.cc
    lua/tuple.cc
    lua/slab.cc
    lua/index.cc
    lua/space.cc
    lua/info.cc
    lua/stat.cc
    lua/error.cc
    lua/session.cc
    ${bin_sources})
