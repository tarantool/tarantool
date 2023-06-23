#
# Set compile flags for entire src/ directory
#
enable_tnt_compile_flags()

include_directories(${LIBEV_INCLUDE_DIR})
include_directories(${LIBEIO_INCLUDE_DIR})
include_directories(${LIBCORO_INCLUDE_DIR})
include_directories(${LUAJIT_INCLUDE_DIRS})
include_directories(${READLINE_INCLUDE_DIRS})
include_directories(${LIBYAML_INCLUDE_DIRS})
include_directories(${MSGPUCK_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${ICU_INCLUDE_DIRS})
include_directories(${ICONV_INCLUDE_DIRS})
include_directories(${DECNUMBER_INCLUDE_DIR})
include_directories(${EXTRA_CORE_INCLUDE_DIRS})
include_directories(SYSTEM ${LIBUNWIND_INCLUDE_DIR})

set(LIBUTIL_FREEBSD_SRC ${PROJECT_SOURCE_DIR}/third_party/libutil_freebsd)
include_directories(${LIBUTIL_FREEBSD_SRC})
include_directories(${EXTRA_CORE_INCLUDE_DIRS})

if(EMBED_LUAZLIB)
    include_directories(${ZLIB_INCLUDE_DIRS})
endif()

if(EMBED_LUAZIP)
    include_directories(${ZZIP_INCLUDE_DIRS})
endif()

# Compile src/lua/*.lua files into src/lua/*.lua.c sources
set(lua_sources)
lua_source(lua_sources lua/minifio.lua minifio_lua)
lua_source(lua_sources lua/loaders.lua loaders_lua)
lua_source(lua_sources lua/init.lua init_lua)
lua_source(lua_sources lua/debug.lua debug_lua)
lua_source(lua_sources lua/dobytecode.lua dobytecode_lua)
lua_source(lua_sources lua/dojitcmd.lua dojitcmd_lua)
lua_source(lua_sources lua/string.lua string_lua)
lua_source(lua_sources lua/fiber.lua fiber_lua)
lua_source(lua_sources lua/buffer.lua buffer_lua)
lua_source(lua_sources lua/uuid.lua uuid_lua)
lua_source(lua_sources lua/crypto.lua crypto_lua)
lua_source(lua_sources lua/error.lua error_lua)
lua_source(lua_sources lua/digest.lua digest_lua)
lua_source(lua_sources lua/msgpackffi.lua msgpackffi_lua)
lua_source(lua_sources lua/uri.lua uri_lua)
lua_source(lua_sources lua/socket.lua socket_lua)
lua_source(lua_sources lua/errno.lua errno_lua)
lua_source(lua_sources lua/log.lua log_lua)
lua_source(lua_sources lua/help.lua help_lua)
lua_source(lua_sources lua/help_en_US.lua help_en_US_lua)
lua_source(lua_sources lua/tap.lua tap_lua)
lua_source(lua_sources lua/fio.lua fio_lua)
lua_source(lua_sources lua/csv.lua csv_lua)
lua_source(lua_sources lua/strict.lua strict_lua)
lua_source(lua_sources lua/clock.lua clock_lua)
lua_source(lua_sources lua/title.lua title_lua)
lua_source(lua_sources lua/argparse.lua argparse_lua)
lua_source(lua_sources lua/env.lua env_lua)
lua_source(lua_sources lua/pwd.lua pwd_lua)
lua_source(lua_sources lua/trigger.lua trigger_lua)
lua_source(lua_sources lua/table.lua table_lua)
lua_source(lua_sources lua/httpc.lua httpc_lua)
lua_source(lua_sources lua/iconv.lua iconv_lua)
lua_source(lua_sources lua/swim.lua swim_lua)
lua_source(lua_sources lua/datetime.lua datetime_lua)
lua_source(lua_sources lua/timezones.lua timezones_lua)
lua_source(lua_sources lua/print.lua print_lua)
lua_source(lua_sources lua/pairs.lua pairs_lua)
lua_source(lua_sources lua/compat.lua compat_lua)
lua_source(lua_sources lua/varbinary.lua varbinary_lua)
if (ENABLE_COMPRESS_MODULE)
    lua_source(lua_sources ${COMPRESS_MODULE_LUA_SOURCE} compress_lua)
endif()
if (ENABLE_ETCD_CLIENT)
    lua_multi_source(lua_sources ${ETCD_CLIENT_LUA_SOURCES})
endif()

# 3rd party lua sources
lua_source(lua_sources ../third_party/luafun/fun.lua fun_lua)
lua_source(lua_sources ../third_party/lua/luadebug.lua luadebug_lua)
lua_source(lua_sources ../third_party/checks/checks/version.lua checks_version_lua)
lua_source(lua_sources ../third_party/checks/checks.lua checks_lua)
lua_source(lua_sources ../third_party/metrics/metrics/api.lua metrics_api_lua)
lua_source(lua_sources ../third_party/metrics/metrics/cartridge/failover.lua metrics_cartridge_failover_lua)
lua_source(lua_sources ../third_party/metrics/metrics/cartridge/issues.lua metrics_cartridge_issues_lua)
lua_source(lua_sources ../third_party/metrics/metrics/cfg.lua metrics_cfg_lua)
lua_source(lua_sources ../third_party/metrics/metrics/collectors/counter.lua metrics_collectors_counter_lua)
lua_source(lua_sources ../third_party/metrics/metrics/collectors/gauge.lua metrics_collectors_gauge_lua)
lua_source(lua_sources ../third_party/metrics/metrics/collectors/histogram.lua metrics_collectors_histogram_lua)
lua_source(lua_sources ../third_party/metrics/metrics/collectors/shared.lua metrics_collectors_shared_lua)
lua_source(lua_sources ../third_party/metrics/metrics/collectors/summary.lua metrics_collectors_summary_lua)
lua_source(lua_sources ../third_party/metrics/metrics/const.lua metrics_const_lua)
lua_source(lua_sources ../third_party/metrics/metrics/http_middleware.lua metrics_http_middleware_lua)
lua_source(lua_sources ../third_party/metrics/metrics/init.lua metrics_lua)
lua_source(lua_sources ../third_party/metrics/metrics/plugins/graphite.lua metrics_plugins_graphite_lua)
lua_source(lua_sources ../third_party/metrics/metrics/plugins/json.lua metrics_plugins_json_lua)
lua_source(lua_sources ../third_party/metrics/metrics/plugins/prometheus.lua metrics_plugins_prometheus_lua)
lua_source(lua_sources ../third_party/metrics/metrics/psutils/cpu.lua metrics_psutils_cpu_lua)
lua_source(lua_sources ../third_party/metrics/metrics/psutils/psutils_linux.lua metrics_psutils_psutils_linux_lua)
lua_source(lua_sources ../third_party/metrics/metrics/quantile.lua metrics_quantile_lua)
lua_source(lua_sources ../third_party/metrics/metrics/registry.lua metrics_registry_lua)
lua_source(lua_sources ../third_party/metrics/metrics/stash.lua metrics_stash_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/clock.lua metrics_tarantool_clock_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/cpu.lua metrics_tarantool_cpu_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/event_loop.lua metrics_tarantool_event_loop_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/fibers.lua metrics_tarantool_fibers_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/info.lua metrics_tarantool_info_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/luajit.lua metrics_tarantool_luajit_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/memory.lua metrics_tarantool_memory_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/memtx.lua metrics_tarantool_memtx_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/network.lua metrics_tarantool_network_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/operations.lua metrics_tarantool_operations_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/replicas.lua metrics_tarantool_replicas_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/runtime.lua metrics_tarantool_runtime_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/slab.lua metrics_tarantool_slab_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/spaces.lua metrics_tarantool_spaces_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/system.lua metrics_tarantool_system_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool/vinyl.lua metrics_tarantool_vinyl_lua)
lua_source(lua_sources ../third_party/metrics/metrics/tarantool.lua metrics_tarantool_lua)
lua_source(lua_sources ../third_party/metrics/metrics/utils.lua metrics_utils_lua)
lua_source(lua_sources ../third_party/metrics/metrics/version.lua metrics_version_lua)

# LuaJIT jit.* library
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/bc.lua jit_bc_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/bcsave.lua jit_bcsave_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dis_arm64.lua jit_dis_arm64_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dis_x86.lua jit_dis_x86_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dis_x64.lua jit_dis_x64_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dump.lua jit_dump_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/v.lua jit_v_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/p.lua jit_p_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/zone.lua jit_zone_lua)
lua_source(lua_sources ${LUAJIT_BINARY_ROOT}/src/jit/vmdef.lua jit_vmdef_lua)
# LuaJIT tools.* library
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof.lua memprof_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof/humanize.lua memprof_humanize_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof/parse.lua memprof_parse_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof/process.lua memprof_process_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/sysprof.lua sysprof_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/sysprof/collapse.lua sysprof_collapse_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/sysprof/parse.lua sysprof_parse_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/utils/avl.lua utils_avl_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/utils/bufread.lua utils_bufread_lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/utils/symtab.lua utils_symtab_lua)

if(EMBED_LUAROCKS)
    include("luarocks_lua_sources.cmake")
endif()

add_custom_target(generate_lua_sources
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

# There is no libdl.so on FreeBSD prior to 11.2.
#
# Always links pthread and dl dynamically.
set(generic_libraries pthread)
if(NOT TARGET_OS_OPENBSD)
    find_library(DL_LIBRARY NAMES dl)
    if(NOT "${DL_LIBRARY}" STREQUAL "DL_LIBRARY-NOTFOUND")
        set(generic_libraries ${generic_libraries} dl)
    endif()
endif()

add_library(stat STATIC rmean.c latency.c histogram.c)
target_link_libraries(stat core)

add_library(cpu_feature STATIC cpu_feature.c)

add_library(crc32 STATIC
    crc32.c
    ${PROJECT_SOURCE_DIR}/third_party/crc32_impl.c
)
target_link_libraries(crc32 cpu_feature)

add_library(shutdown STATIC on_shutdown.c)

set (server_sources
     find_path.c
     curl.c
     httpc.c
     pickle.c
     cfg.c
     title.c
     proc_title.c
     path_lock.c
     ssl_cert_paths_discover.c
     systemd.c
     version.c
     lua/digest.c
     lua/init.c
     lua/fiber.c
     lua/fiber_cond.c
     lua/fiber_channel.c
     lua/trigger.c
     lua/msgpack.c
     lua/utils.c
     lua/serializer.c
     lua/errno.c
     lua/tnt_iconv.c
     lua/tnt_msgpuck.c
     lua/tnt_readline.c
     lua/tnt_datetime.c
     lua/error.c
     lua/socket.c
     lua/pickle.c
     lua/minifio.c
     lua/fio.c
     lua/popen.c
     lua/httpc.c
     lua/utf8.c
     lua/info.c
     lua/string.c
     lua/swim.c
     lua/decimal.c
     lua/uri.c
     lua/backtrace.c
     lua/builtin_modcache.c
     lua/tweaks.c
     lua/xml.c
     ${lua_sources}
     ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/lyaml.cc
     ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/lua_cjson.c
     ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/strbuf.c
     ${COMPRESS_MODULE_SOURCES}
)

if(EMBED_LUAZLIB)
    list(APPEND server_sources
        ${PROJECT_SOURCE_DIR}/third_party/lua-zlib/lua_zlib.c)
endif()

if(EMBED_LUAZIP)
    list(APPEND server_sources
        ${PROJECT_SOURCE_DIR}/third_party/luazip/src/luazip.c)
endif()

# List of header files to scan for the module API declarations.
#
# Blocks of the following kind are extracted from the headers and
# placed into the module.h file.
#
# /** \cond public */
# <...>
# /** \endcond public */
set(api_headers
    ${PROJECT_BINARY_DIR}/src/trivia/config.h
    ${PROJECT_SOURCE_DIR}/src/trivia/util.h
    ${PROJECT_SOURCE_DIR}/src/on_shutdown.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/say.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/fiber.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/fiber_cond.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/coio.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/coio_task.h
    ${PROJECT_SOURCE_DIR}/src/box/ibuf.h
    ${PROJECT_SOURCE_DIR}/src/lua/utils.h
    ${PROJECT_SOURCE_DIR}/src/lua/error.h
    ${PROJECT_SOURCE_DIR}/src/box/txn.h
    ${PROJECT_SOURCE_DIR}/src/box/tuple.h
    ${PROJECT_SOURCE_DIR}/src/box/key_def.h
    ${PROJECT_SOURCE_DIR}/src/box/field_def.h
    ${PROJECT_SOURCE_DIR}/src/box/tuple_format.h
    ${PROJECT_SOURCE_DIR}/src/box/schema_def.h
    ${PROJECT_SOURCE_DIR}/src/box/schema.h
    ${PROJECT_SOURCE_DIR}/src/box/box.h
    ${PROJECT_SOURCE_DIR}/src/box/index.h
    ${PROJECT_SOURCE_DIR}/src/box/iterator_type.h
    ${PROJECT_SOURCE_DIR}/src/box/error.h
    ${PROJECT_SOURCE_DIR}/src/box/lua/tuple.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/latch.h
    ${PROJECT_SOURCE_DIR}/src/lib/core/clock.h
    ${PROJECT_SOURCE_DIR}/src/box/decimal.h
    ${PROJECT_SOURCE_DIR}/src/lua/decimal.h
    ${EXTRA_API_HEADERS}
)
rebuild_module_api(${api_headers})

if (NOT TARGET_OS_DEBIAN_FREEBSD)
    if (TARGET_OS_FREEBSD)
        set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/src/proc_title.c
        PROPERTIES COMPILE_FLAGS "-DHAVE_SETPROCTITLE")
    endif()
endif()

set_source_files_compile_flags(${server_sources})
add_library(server STATIC ${server_sources})
add_dependencies(server build_bundled_libs)
target_link_libraries(server core coll http_parser bit uri swim swim_udp
                      swim_ev crypto mpstream crc32 tzcode)
target_include_directories(server PRIVATE ${EXTRA_SERVER_INCLUDE_DIRS})

if(EMBED_LUAZLIB)
    target_link_libraries(server ${ZLIB_LIBRARIES})
endif()

if(EMBED_LUAZIP)
    target_link_libraries(server ${ZZIP_LIBRARIES})
endif()

# Rule of thumb: if exporting a symbol from a static library, list the
# library here.
set (reexport_libraries server core misc bitset csv swim swim_udp swim_ev
     shutdown tzcode ${LUAJIT_LIBRARIES} ${MSGPUCK_LIBRARIES} ${ICU_LIBRARIES}
     ${CURL_LIBRARIES} ${XXHASH_LIBRARIES} ${LIBCDT_LIBRARIES}
     ${EXTRA_REEXPORT_LIBRARIES})

set (common_libraries
    ${reexport_libraries}
    ${LIBYAML_LIBRARIES}
    ${READLINE_LIBRARIES}
    ${ICONV_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

if (TARGET_OS_LINUX OR TARGET_OS_DEBIAN_FREEBSD)
    set (common_libraries ${common_libraries} dl rt)
endif()

if (TARGET_OS_FREEBSD AND NOT TARGET_OS_DEBIAN_FREEBSD)
    find_library (INTL intl)
    if (NOT INTL)
        message(FATAL_ERROR "intl library not found")
    else()
        set (common_libraries ${common_libraries} ${INTL})
    endif()
endif()

set (common_libraries ${common_libraries} PARENT_SCOPE)

add_subdirectory(lib)
add_subdirectory(box)

include_directories(${EXTRA_BOX_INCLUDE_DIRS})

# Save CMAKE_XXX_FLAGS from this directory for config.h (used in --version)
set(TARANTOOL_C_FLAGS ${CMAKE_C_FLAGS} PARENT_SCOPE)
set(TARANTOOL_CXX_FLAGS ${CMAKE_CXX_FLAGS} PARENT_SCOPE)

set(EXPORT_LIST)
if(BUILD_STATIC)
    # for each static library we should find a corresponding shared library to
    # parse and reexport library api functions
    foreach(libstatic
            ${READLINE_LIBRARIES}
            ${CURL_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${ICU_LIBRARIES})
        if (${libstatic} MATCHES "lib[^/]+.a")
            string(REGEX MATCH "lib[^/]+.a" libname ${libstatic})
            string(REGEX REPLACE "\\.a$" "" libname ${libname})
            string(REGEX REPLACE "^lib" "" libname ${libname})
            find_library(SYMBOLS_LIB NAMES ${libname})
            # add found library to export list
            list(APPEND EXPORT_LIST ${SYMBOLS_LIB})
            # set variable to allow rescan (CMake depended)
            set(SYMBOLS_LIB "SYMBOLS_LIB-NOTFOUND")
        elseif (${libstatic} STREQUAL bundled-libcurl OR
                ${libstatic} STREQUAL bundled-ares OR
                ${libstatic} STREQUAL bundled-nghttp2)
            message("We don't need to export symbols from statically linked ${libstatic}, skipped")
        else()
            message(WARNING "${libstatic} should be a static")
        endif()
    endforeach(libstatic)
    string(REPLACE ";" " " EXPORT_LIST "${EXPORT_LIST}")
endif()

set(exports_file_sources
    ${PROJECT_SOURCE_DIR}/extra/exports
    ${EXTRA_EXPORTS_FILE_SOURCES})
if (EXPORT_LIBCURL_SYMBOLS)
    set(exports_file_sources ${exports_file_sources}
        ${PROJECT_SOURCE_DIR}/extra/exports_libcurl)
endif()
string(REPLACE ";" " " exports_file_sources_str "${exports_file_sources}")

# Exports syntax is toolchain-dependent, preprocessing is necessary
set(exports_file ${PROJECT_BINARY_DIR}/extra/exports.${CMAKE_SYSTEM_NAME})
add_custom_target(preprocess_exports
                  DEPENDS ${exports_file})
add_custom_command(
    OUTPUT  ${exports_file}
    DEPENDS ${exports_file_sources}
    COMMAND ${PROJECT_SOURCE_DIR}/extra/mkexports
            ${exports_file_sources_str}
            ${exports_file} ${CMAKE_SYSTEM_NAME}
            ${EXPORT_LIST}
)

add_executable(
    tarantool main.cc
    ${LIBUTIL_FREEBSD_SRC}/flopen.c
    ${LIBUTIL_FREEBSD_SRC}/pidfile.c)

add_dependencies(tarantool build_bundled_libs preprocess_exports)

# Re-link if exports changed
set_target_properties(tarantool PROPERTIES LINK_DEPENDS ${exports_file})

# TODO(gh-7424): Autogenerate the list of internal symbols.
add_library(symbols STATIC symbols.c)

# A note about linkers:
# [GNU linker] When linking an *executable* visibility is ignored, and
#              either nothing is exported (default), or any non-static
#              symbol is exported (-rdynamic), or explicitly listed
#              symbols are exported (--dynamic-list).
#
#              However, if a symbol listed lives in a static library it
#              won't be automatically pulled, hence --whole-archive
#              option.
#
# [Apple linker] One can provide an explicit export list; pulls symbols
#                from static libraries.
#
if (TARGET_OS_DARWIN)
    target_link_libraries(tarantool box symbols ${common_libraries})
    set_target_properties(tarantool PROPERTIES
        LINK_FLAGS "-Wl,-exported_symbols_list,${exports_file}")
else ()
    target_link_libraries(tarantool
                          -Wl,--whole-archive box ${reexport_libraries}
                          salad symbols -Wl,--no-whole-archive
                          ${common_libraries}
                          ${generic_libraries})
    set_target_properties(tarantool PROPERTIES
        LINK_FLAGS "-Wl,--dynamic-list,${exports_file}")
    # CMake 3.3 and below, for historical reasons, always linked
    # executables on some platforms with flags like -rdynamic to
    # export symbols from the executables for use by any plugins
    # they may load via dlopen.
    # https://cmake.org/cmake/help/latest/policy/CMP0065.html
    #
    # So, while we are using CMake versions below 3.4, we need
    # to get rid of the "rdynamic" flag.
    set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
endif()

install (TARGETS tarantool DESTINATION bin)
