file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)

include_directories(${SOPHIA_INCLUDE_DIR})

set(lua_sources)
lua_source(lua_sources lua/load_cfg.lua)
lua_source(lua_sources lua/schema.lua)
lua_source(lua_sources lua/tuple.lua)
lua_source(lua_sources lua/session.lua)
lua_source(lua_sources lua/snapshot_daemon.lua)
set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

add_library(box
    iproto.cc
    iproto_constants.c
    iproto_port.cc
    errcode.c
    error.cc
    xrow.cc
    xlog.cc
    tuple.cc
    tuple_convert.cc
    tuple_update.cc
    key_def.cc
    index.cc
    memtx_hash.cc
    memtx_tree.cc
    memtx_rtree.cc
    memtx_bitset.cc
    engine.cc
    memtx_engine.cc
    sysview_engine.cc
    sysview_index.cc
    sophia_engine.cc
    sophia_index.cc
    space.cc
    alter.cc
    schema.cc
    session.cc
    port.cc
    request.cc
    txn.cc
    box.cc
    user_def.cc
    user.cc
    authentication.cc
    vclock.c
    cluster.cc
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

target_link_libraries(box ${sophia_lib})
