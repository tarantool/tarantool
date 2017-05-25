file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)

set(lua_sources)
lua_source(lua_sources lua/load_cfg.lua)
lua_source(lua_sources lua/schema.lua)
lua_source(lua_sources lua/tuple.lua)
lua_source(lua_sources lua/session.lua)
lua_source(lua_sources lua/snapshot_daemon.lua)
lua_source(lua_sources lua/net_box.lua)
lua_source(lua_sources lua/upgrade.lua)
lua_source(lua_sources lua/console.lua)
set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

include_directories(${ZSTD_INCLUDE_DIRS})

add_library(box STATIC
    iproto.cc
    iproto_constants.c
    iproto_port.cc
    errcode.c
    error.cc
    xrow.cc
    xrow_io.cc
    xlog.cc
    tuple_format.c
    tuple.c
    tuple_convert.c
    tuple_update.c
    tuple_compare.cc
    tuple_hash.cc
    key_def.cc
    index.cc
    memtx_index.cc
    memtx_hash.cc
    memtx_tree.cc
    memtx_rtree.cc
    memtx_bitset.cc
    engine.cc
    memtx_engine.cc
    memtx_space.cc
    memtx_tuple.cc
    sysview_engine.cc
    sysview_index.cc
    vinyl_engine.cc
    vinyl_space.cc
    vinyl_index.cc
    vinyl.c
    vy_stmt.c
    vy_mem.c
    vy_run.c
    vy_cache.c
    vy_log.c
    vy_upsert.c
    space.cc
    func.cc
    alter.cc
    schema.cc
    session.cc
    port.cc
    request.c
    txn.cc
    box.cc
    gc.c
    user_def.c
    user.cc
    authentication.cc
    vclock.c
    replication.cc
    recovery.cc
    xstream.cc
    applier.cc
    relay.cc
    journal.c
    wal.cc
    ${lua_sources}
    lua/init.c
    lua/call.c
    lua/cfg.cc
    lua/console.c
    lua/tuple.c
    lua/slab.c
    lua/index.c
    lua/space.cc
    lua/misc.cc
    lua/info.c
    lua/stat.c
    lua/error.cc
    lua/session.c
    lua/net_box.c
    lua/xlog.c
    ${bin_sources})

target_link_libraries(box ${ZSTD_LIBRARIES} ${common_libraries})
add_dependencies(box build_bundled_libs)
