file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/lua)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/box/sql)

# Sometimes 'spying' code is not acceptable even if it would be
# disabled by default. That option allows to remove the feedback
# daemon from the build completely.
option(ENABLE_FEEDBACK_DAEMON "Feedback daemon which reports debug data to the Tarantool team" ON)

set(lua_sources)
lua_source(lua_sources lua/load_cfg.lua)
lua_source(lua_sources lua/schema.lua)
lua_source(lua_sources lua/tuple.lua)
lua_source(lua_sources lua/session.lua)
if (ENABLE_FEEDBACK_DAEMON)
    lua_source(lua_sources lua/feedback_daemon.lua)
endif()
lua_source(lua_sources lua/net_box.lua)
lua_source(lua_sources lua/upgrade.lua)
lua_source(lua_sources lua/console.lua)
lua_source(lua_sources lua/xlog.lua)
lua_source(lua_sources lua/key_def.lua)
lua_source(lua_sources lua/merger.lua)
set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h)

set(sql_sources
    sql/opcodes.c
    sql/parse.c
    sql/alter.c
    sql/analyze.c
    sql/cursor.c
    sql/build.c
    sql/callback.c
    sql/date.c
    sql/delete.c
    sql/expr.c
    sql/fk_constraint.c
    sql/func.c
    sql/global.c
    sql/hash.c
    sql/insert.c
    sql/legacy.c
    sql/main.c
    sql/malloc.c
    sql/mem.c
    sql/os.c
    sql/os_unix.c
    sql/parse_def.c
    sql/pragma.c
    sql/prepare.c
    sql/printf.c
    sql/random.c
    sql/resolve.c
    sql/select.c
    sql/tokenize.c
    sql/treeview.c
    sql/trigger.c
    sql/utf.c
    sql/update.c
    sql/util.c
    sql/vdbe.c
    sql/vdbeapi.c
    sql/vdbeaux.c
    sql/vdbemem.c
    sql/vdbesort.c
    sql/vdbetrace.c
    sql/walker.c
    sql/where.c
    sql/wherecode.c
    sql/whereexpr.c
)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

include_directories(${ZSTD_INCLUDE_DIRS})
include_directories(${CMAKE_BINARY_DIR}/src/box/sql)
include_directories(${CMAKE_BINARY_DIR}/src/box)

add_library(box_error STATIC error.cc errcode.c mp_error.cc)
target_link_libraries(box_error core stat mpstream vclock)

add_library(xrow STATIC xrow.c iproto_constants.c)
target_link_libraries(xrow server core small vclock misc box_error
                      scramble ${MSGPUCK_LIBRARIES})

add_library(tuple STATIC
    tuple.c
    field_map.c
    tuple_format.c
    xrow_update.c
    xrow_update_field.c
    xrow_update_array.c
    xrow_update_bar.c
    xrow_update_route.c
    xrow_update_map.c
    tuple_compare.cc
    tuple_extract_key.cc
    tuple_hash.cc
    tuple_bloom.c
    tuple_dictionary.c
    key_def.c
    coll_id_def.c
    coll_id.c
    coll_id_cache.c
    field_def.c
    opt_def.c
)
target_link_libraries(tuple json box_error core ${MSGPUCK_LIBRARIES} ${ICU_LIBRARIES} misc bit)

add_library(xlog STATIC xlog.c)
target_link_libraries(xlog core box_error crc32 ${ZSTD_LIBRARIES})

add_library(box STATIC
    msgpack.c
    iproto.cc
    xrow_io.cc
    tuple_convert.c
    identifier.c
    index.cc
    index_def.c
    iterator_type.c
    memtx_hash.c
    memtx_tree.cc
    memtx_rtree.c
    memtx_bitset.c
    memtx_tx.c
    module_cache.c
    engine.c
    memtx_engine.c
    memtx_space.c
    sysview.c
    blackhole.c
    service_engine.c
    session_settings.c
    vinyl.c
    vy_stmt.c
    vy_mem.c
    vy_run.c
    vy_range.c
    vy_lsm.c
    vy_tx.c
    vy_write_iterator.c
    vy_read_iterator.c
    vy_point_lookup.c
    vy_cache.c
    vy_log.c
    vy_upsert.c
    vy_history.c
    vy_read_set.c
    vy_scheduler.c
    vy_regulator.c
    vy_quota.c
    request.c
    space.c
    space_def.c
    sequence.c
    ck_constraint.c
    fk_constraint.c
    constraint_id.c
    func.c
    func_def.c
    key_list.c
    alter.cc
    schema.cc
    schema_def.c
    session.cc
    port.c
    txn.c
    txn_limbo.c
    raft.c
    box.cc
    gc.c
    checkpoint_schedule.c
    user_def.c
    user.cc
    authentication.cc
    replication.cc
    recovery.cc
    xstream.cc
    applier.cc
    relay.cc
    journal.c
    sql.c
    bind.c
    execute.c
    sql_stmt_cache.c
    wal.c
    call.c
    merger.c
    ibuf.c
    ${sql_sources}
    ${lua_sources}
    lua/init.c
    lua/call.c
    lua/cfg.cc
    lua/console.c
    lua/lib.c
    lua/serialize_lua.c
    lua/tuple.c
    lua/slab.c
    lua/index.c
    lua/space.cc
    lua/sequence.c
    lua/misc.cc
    lua/info.c
    lua/stat.c
    lua/ctl.c
    lua/error.cc
    lua/session.c
    lua/net_box.c
    lua/xlog.c
    lua/execute.c
    lua/key_def.c
    lua/merger.c
    ${bin_sources})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DSQL_DEBUG=1)
endif()
add_definitions(-DSQL_TEST=1)

set(EXT_SRC_DIR ${CMAKE_SOURCE_DIR}/extra)
set(EXT_BIN_DIR ${CMAKE_BINARY_DIR}/extra)
set(SQL_SRC_DIR ${CMAKE_SOURCE_DIR}/src/box/sql)
set(SQL_BIN_DIR ${CMAKE_BINARY_DIR}/src/box/sql)

include_directories(${SQL_SRC_DIR})
include_directories(${SQL_BIN_DIR})

add_custom_target(generate_sql_files DEPENDS
    sql/parse.h
    sql/keywordhash.h
    sql/parse.y
    sql/parse.c
    sql/opcodes.c)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/keywordhash.h
    COMMAND ${EXT_BIN_DIR}/mkkeywordhash > keywordhash.h.tmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different keywordhash.h.tmp keywordhash.h
    COMMAND ${CMAKE_COMMAND} -E remove keywordhash.h.tmp
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS mkkeywordhash)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/parse.h ${SQL_BIN_DIR}/parse.c
    COMMAND ${EXT_BIN_DIR}/lemon -T${EXT_SRC_DIR}/lempar.c -o${SQL_BIN_DIR}/parse.c ${SQL_SRC_DIR}/parse.y
    COMMAND ${CMAKE_COMMAND} -E copy parse.h parse.h.tmp
    COMMAND ${EXT_SRC_DIR}/addopcodes.sh parse.h.tmp > parse.h
    COMMAND ${CMAKE_COMMAND} -E remove parse.h.tmp parse.out
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS lemon ${SQL_SRC_DIR}/parse.y)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/opcodes.h
    COMMAND cat parse.h ${SQL_SRC_DIR}/vdbe.c | ${EXT_SRC_DIR}/mkopcodeh.sh > opcodes.h
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS ${SQL_SRC_DIR}/vdbe.c ${EXT_SRC_DIR}/mkopcodeh.sh ${SQL_BIN_DIR}/parse.h)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/opcodes.c
    COMMAND ${EXT_SRC_DIR}/mkopcodec.sh opcodes.h > opcodes.c
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS ${SQL_SRC_DIR}/vdbe.c ${EXT_SRC_DIR}/mkopcodec.sh ${SQL_BIN_DIR}/parse.h
        ${SQL_BIN_DIR}/opcodes.h)

target_link_libraries(box box_error tuple stat xrow xlog vclock crc32 scramble
                      raft ${common_libraries})

add_dependencies(box build_bundled_libs generate_sql_files)
