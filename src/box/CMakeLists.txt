file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/src/box/lua)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/src/box/sql)

# Sometimes 'spying' code is not acceptable even if it would be
# disabled by default. That option allows to remove the feedback
# daemon from the build completely.
option(ENABLE_FEEDBACK_DAEMON "Feedback daemon which reports debug data to the Tarantool team" ON)

set(lua_sources)
lua_source(lua_sources lua/load_cfg.lua load_cfg_lua)
lua_source(lua_sources lua/schema.lua schema_lua)
lua_source(lua_sources lua/tuple.lua tuple_lua)
lua_source(lua_sources lua/tuple_format.lua tuple_format_lua)
lua_source(lua_sources lua/session.lua session_lua)
if (ENABLE_FEEDBACK_DAEMON)
    lua_source(lua_sources lua/feedback_daemon.lua feedback_daemon_lua)
endif()
if(ENABLE_AUDIT_LOG)
    lua_source(lua_sources ${AUDIT_LOG_LUA_SOURCE} audit_lua)
endif()
if(ENABLE_SPACE_UPGRADE)
    lua_source(lua_sources ${SPACE_UPGRADE_LUA_SOURCE} space_upgrade_lua)
endif()
if(ENABLE_FLIGHT_RECORDER)
    lua_source(lua_sources ${FLIGHT_RECORDER_LUA_SOURCE} flightrec_lua)
endif()
if(ENABLE_READ_VIEW)
    lua_source(lua_sources ${READ_VIEW_LUA_SOURCE} read_view_lua)
endif()
if(ENABLE_SECURITY)
    lua_source(lua_sources ${SECURITY_LUA_SOURCE} security_lua)
endif()
if(ENABLE_INTEGRITY)
    lua_source(lua_sources ${INTEGRITY_LUA_SOURCE} integrity_lua)
endif()
lua_source(lua_sources lua/net_box.lua net_box_lua)
lua_source(lua_sources lua/upgrade.lua upgrade_lua)
lua_source(lua_sources lua/console.lua console_lua)
lua_source(lua_sources lua/xlog.lua xlog_lua)
lua_source(lua_sources lua/key_def.lua key_def_lua)
lua_source(lua_sources lua/merger.lua merger_lua)
lua_source(lua_sources lua/iproto.lua iproto_lua)
lua_source(lua_sources lua/mkversion.lua mkversion_lua)

# {{{ config
lua_source(lua_sources lua/config/applier/app.lua         config_applier_app_lua)
lua_source(lua_sources lua/config/applier/box_cfg.lua     config_applier_box_cfg_lua)
lua_source(lua_sources lua/config/applier/compat.lua      config_applier_compat_lua)
lua_source(lua_sources lua/config/applier/console.lua     config_applier_console_lua)
lua_source(lua_sources lua/config/applier/credentials.lua config_applier_credentials_lua)
lua_source(lua_sources lua/config/applier/fiber.lua       config_applier_fiber_lua)
lua_source(lua_sources lua/config/applier/mkdir.lua       config_applier_mkdir_lua)
lua_source(lua_sources lua/config/applier/roles.lua       config_applier_roles_lua)
lua_source(lua_sources lua/config/applier/sharding.lua    config_applier_sharding_lua)
lua_source(lua_sources lua/config/cluster_config.lua      config_cluster_config_lua)
lua_source(lua_sources lua/config/configdata.lua          config_configdata_lua)
lua_source(lua_sources lua/config/init.lua                config_init_lua)
lua_source(lua_sources lua/config/instance_config.lua     config_instance_config_lua)
lua_source(lua_sources lua/config/source/env.lua          config_source_env_lua)
lua_source(lua_sources lua/config/source/file.lua         config_source_file_lua)
lua_source(lua_sources lua/config/utils/aboard.lua        config_utils_aboard_lua)
lua_source(lua_sources lua/config/utils/expression.lua    config_utils_expression_lua)
lua_source(lua_sources lua/config/utils/file.lua          config_utils_file_lua)
lua_source(lua_sources lua/config/utils/log.lua           config_utils_log_lua)
lua_source(lua_sources lua/config/utils/odict.lua         config_utils_odict_lua)
lua_source(lua_sources lua/config/utils/schema.lua        config_utils_schema_lua)
lua_source(lua_sources lua/config/utils/snapshot.lua      config_utils_snapshot_lua)
lua_source(lua_sources lua/config/utils/tabulate.lua      config_utils_tabulate_lua)

if (ENABLE_CONFIG_EXTRAS)
    lua_source(lua_sources ${CONFIG_EXTRAS_DIR}/source/etcd.lua config_source_etcd_lua)
    lua_source(lua_sources ${CONFIG_EXTRAS_DIR}/storage/init.lua config_storage_init_lua)
    lua_source(lua_sources ${CONFIG_EXTRAS_DIR}/source/storage.lua config_source_storage_lua)
    lua_source(lua_sources ${CONFIG_EXTRAS_DIR}/extras.lua config_extras_lua)
endif()
# }}} config

if (ENABLE_FAILOVER)
    lua_multi_source(lua_sources ${FAILOVER_LUA_SOURCES})
endif()

set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h bootstrap_bin)

set(sql_sources
    sql/opcodes.c
    sql/parse.c
    sql/alter.c
    sql/cursor.c
    sql/build.c
    sql/delete.c
    sql/expr.c
    sql/func.c
    sql/global.c
    sql/hash.c
    sql/insert.c
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
    sql/port.c
    sql/select.c
    sql/show.c
    sql/tokenize.c
    sql/treeview.c
    sql/trigger.c
    sql/update.c
    sql/util.c
    sql/vdbe.c
    sql/vdbeapi.c
    sql/vdbeaux.c
    sql/vdbesort.c
    sql/vdbetrace.c
    sql/walker.c
    sql/where.c
    sql/wherecode.c
    sql/whereexpr.c
)

add_custom_target(box_generate_lua_sources
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

include_directories(${ZSTD_INCLUDE_DIRS})
include_directories(${PROJECT_BINARY_DIR}/src/box/sql)
include_directories(${PROJECT_BINARY_DIR}/src/box)
include_directories(${EXTRA_CORE_INCLUDE_DIRS})
include_directories(${EXTRA_BOX_INCLUDE_DIRS})

add_library(box_error STATIC error.cc errcode.c mp_error.cc)
target_link_libraries(box_error core stat mpstream vclock)

add_library(node_name STATIC node_name.c)

add_library(xrow STATIC xrow.c iproto_constants.c iproto_features.c)
target_link_libraries(xrow server core small vclock misc box_error node_name
                      ${MSGPUCK_LIBRARIES})

set(tuple_sources
    tuple.c
    field_map.c
    tuple_format.c
    tuple_format_map.c
    tuple_constraint_def.c
    tuple_constraint.c
    tuple_builder.c
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
    identifier.c
    mp_tuple.c
)

if(ENABLE_TUPLE_COMPRESSION)
    list(APPEND tuple_sources ${TUPLE_COMPRESSION_BOX_SOURCES})
endif()

add_library(tuple STATIC ${tuple_sources})
target_link_libraries(tuple json box_error core ${MSGPUCK_LIBRARIES} misc bit coll)

set(xlog_sources xlog.c)
if(ENABLE_RETENTION_PERIOD)
    list(APPEND xlog_sources ${RETENTION_PERIOD_SOURCES})
endif()

add_library(xlog STATIC ${xlog_sources})
target_link_libraries(xlog core box_error crc32 ${ZSTD_LIBRARIES})

set(box_sources
    allocator.cc
    memtx_allocator.cc
    msgpack.c
    iproto.cc
    xrow_io.cc
    tuple_convert.c
    index.cc
    index_def.c
    index_weak_ref.c
    iterator_type.c
    memtx_hash.cc
    memtx_tree.cc
    memtx_rtree.cc
    memtx_bitset.cc
    memtx_tx.c
    module_cache.c
    engine.c
    memtx_engine.cc
    memtx_space.c
    sysview.c
    sysalloc.c
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
    space_cache.c
    space_def.c
    sequence.c
    func.c
    func_adapter.c
    func_cache.c
    func_def.c
    field_default_func.c
    tuple_constraint_func.c
    tuple_constraint_fkey.c
    key_list.c
    alter.cc
    schema.cc
    schema_def.c
    session.c
    port.c
    txn.c
    txn_limbo.c
    txn_event_trigger.c
    raft.c
    box.cc
    gc.c
    checkpoint_schedule.c
    user_def.c
    user.cc
    authentication.c
    auth_chap_sha1.c
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
    watcher.c
    decimal.c
    read_view.c
    mp_box_ctx.c
    ${sql_sources}
    ${lua_sources}
    lua/init.c
    lua/call.c
    lua/cfg.cc
    lua/console.c
    lua/lib.c
    lua/serialize_lua.c
    lua/tuple.c
    lua/slab.cc
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
    lua/watcher.c
    lua/iproto.c
    lua/func_adapter.c
    lua/tuple_format.c
    lua/trigger.c
    lua/config/utils/expression_lexer.c
    ${bin_sources})

if(ENABLE_AUDIT_LOG)
    list(APPEND box_sources ${AUDIT_LOG_SOURCES})
endif()

if(ENABLE_SPACE_UPGRADE)
    list(APPEND box_sources ${SPACE_UPGRADE_SOURCES})
else()
    list(APPEND box_sources space_upgrade.c memtx_space_upgrade.c)
endif()

if(ENABLE_FLIGHT_RECORDER)
    list(APPEND box_sources ${FLIGHT_RECORDER_SOURCES})
endif()

if(ENABLE_WAL_EXT)
    list(APPEND box_sources ${WAL_EXT_SOURCES})
endif()

if(ENABLE_READ_VIEW)
    list(APPEND box_sources ${READ_VIEW_SOURCES})
endif()

if(ENABLE_SECURITY)
    list(APPEND box_sources ${SECURITY_SOURCES})
endif()

if(ENABLE_INTEGRITY)
    list(APPEND box_sources ${INTEGRITY_SOURCE})
endif()

if(ENABLE_MEMCS_ENGINE)
    list(APPEND box_sources ${MEMCS_ENGINE_SOURCES})
endif()

add_library(box STATIC ${box_sources})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DSQL_DEBUG=1)
endif()
add_definitions(-DSQL_TEST=1)

set(EXT_SRC_DIR ${PROJECT_SOURCE_DIR}/extra)
set(EXT_BIN_DIR ${PROJECT_BINARY_DIR}/extra)
set(SQL_SRC_DIR ${PROJECT_SOURCE_DIR}/src/box/sql)
set(SQL_BIN_DIR ${PROJECT_BINARY_DIR}/src/box/sql)

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

target_link_libraries(box box_error tuple stat xrow xlog vclock crc32 raft
                      node_name ${common_libraries})

add_dependencies(box build_bundled_libs generate_sql_files)
