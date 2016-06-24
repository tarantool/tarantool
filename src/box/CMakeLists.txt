file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)

set(lua_sources)
lua_source(lua_sources lua/load_cfg.lua)
lua_source(lua_sources lua/schema.lua)
lua_source(lua_sources lua/tuple.lua)
lua_source(lua_sources lua/session.lua)
lua_source(lua_sources lua/snapshot_daemon.lua)
lua_source(lua_sources lua/net_box.lua)
lua_source(lua_sources lua/upgrade.lua)
set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

include_directories(${LZ4_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS})

add_library(box STATIC
    iproto.cc
    iproto_constants.c
    iproto_port.cc
    errcode.c
    error.cc
    xrow.cc
    xrow_io.cc
    xlog.cc
    tuple_format.cc
    tuple.cc
    tuple_convert.cc
    tuple_update.cc
    tuple_compare.cc
    key_def.cc
    index.cc
    memtx_index.cc
    memtx_hash.cc
    memtx_tree.cc
    memtx_rtree.cc
    memtx_bitset.cc
    engine.cc
    memtx_engine.cc
    sysview_engine.cc
    sysview_index.cc
    vinyl_engine.cc
    vinyl_space.cc
    vinyl_index.cc
    vinyl.c
    space.cc
    func.cc
    alter.cc
    schema.cc
    session.cc
    port.cc
    request.cc
    txn.cc
    box.cc
    user_def.c
    user.cc
    authentication.cc
    vclock.c
    cluster.cc
    recovery.cc
    applier.cc
    relay.cc
    wal.cc
    ${lua_sources}
    lua/init.c
    lua/call.c
    lua/cfg.cc
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
    ${bin_sources})

target_link_libraries(box ${LZ4_LIBRARIES} ${ZSTD_LIBRARIES})
add_dependencies(box build_bundled_libs)
