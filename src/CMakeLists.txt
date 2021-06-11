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

set(LIBUTIL_FREEBSD_SRC ${CMAKE_SOURCE_DIR}/third_party/libutil_freebsd)
include_directories(${LIBUTIL_FREEBSD_SRC})

# Compile src/lua/*.lua files into src/lua/*.lua.c sources
set(lua_sources)
lua_source(lua_sources lua/init.lua)
lua_source(lua_sources lua/debug.lua)
lua_source(lua_sources lua/string.lua)
lua_source(lua_sources lua/fiber.lua)
lua_source(lua_sources lua/buffer.lua)
lua_source(lua_sources lua/uuid.lua)
lua_source(lua_sources lua/crypto.lua)
lua_source(lua_sources lua/error.lua)
lua_source(lua_sources lua/digest.lua)
lua_source(lua_sources lua/msgpackffi.lua)
lua_source(lua_sources lua/uri.lua)
lua_source(lua_sources lua/socket.lua)
lua_source(lua_sources lua/errno.lua)
lua_source(lua_sources lua/log.lua)
lua_source(lua_sources lua/help.lua)
lua_source(lua_sources lua/help_en_US.lua)
lua_source(lua_sources lua/tap.lua)
lua_source(lua_sources lua/fio.lua)
lua_source(lua_sources lua/csv.lua)
lua_source(lua_sources lua/strict.lua)
lua_source(lua_sources lua/clock.lua)
lua_source(lua_sources lua/title.lua)
lua_source(lua_sources lua/argparse.lua)
lua_source(lua_sources lua/env.lua)
lua_source(lua_sources lua/pwd.lua)
lua_source(lua_sources lua/trigger.lua)
lua_source(lua_sources lua/table.lua)
lua_source(lua_sources ../third_party/luafun/fun.lua)
lua_source(lua_sources lua/httpc.lua)
lua_source(lua_sources lua/iconv.lua)
lua_source(lua_sources lua/swim.lua)
# LuaJIT jit.* library
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/bc.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/bcsave.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dis_arm64.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dis_x86.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dis_x64.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/dump.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/v.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/p.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/src/jit/zone.lua)
lua_source(lua_sources ${LUAJIT_BINARY_ROOT}/src/jit/vmdef.lua)
# LuaJIT tools.* library
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof/humanize.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof/parse.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/memprof/process.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/utils/bufread.lua)
lua_source(lua_sources ${LUAJIT_SOURCE_ROOT}/tools/utils/symtab.lua)

add_custom_target(generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
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

add_library(scramble STATIC scramble.c)
target_link_libraries(scramble core misc)

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
     lua/error.c
     lua/socket.c
     lua/pickle.c
     lua/fio.c
     lua/popen.c
     lua/httpc.c
     lua/utf8.c
     lua/info.c
     lua/string.c
     lua/swim.c
     lua/decimal.c
     ${lua_sources}
     ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/lyaml.cc
     ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/b64.c
     ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/lua_cjson.c
     ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/strbuf.c
)

set(api_headers
    ${CMAKE_BINARY_DIR}/src/trivia/config.h
    ${CMAKE_SOURCE_DIR}/src/trivia/util.h
    ${CMAKE_SOURCE_DIR}/src/on_shutdown.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/say.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/fiber.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/fiber_cond.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/coio.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/coio_task.h
    ${CMAKE_SOURCE_DIR}/src/box/ibuf.h
    ${CMAKE_SOURCE_DIR}/src/lua/utils.h
    ${CMAKE_SOURCE_DIR}/src/lua/error.h
    ${CMAKE_SOURCE_DIR}/src/lua/string.h
    ${CMAKE_SOURCE_DIR}/src/box/txn.h
    ${CMAKE_SOURCE_DIR}/src/box/tuple.h
    ${CMAKE_SOURCE_DIR}/src/box/key_def.h
    ${CMAKE_SOURCE_DIR}/src/box/lua/key_def.h
    ${CMAKE_SOURCE_DIR}/src/box/field_def.h
    ${CMAKE_SOURCE_DIR}/src/box/tuple_format.h
    ${CMAKE_SOURCE_DIR}/src/box/tuple_extract_key.h
    ${CMAKE_SOURCE_DIR}/src/box/schema_def.h
    ${CMAKE_SOURCE_DIR}/src/box/box.h
    ${CMAKE_SOURCE_DIR}/src/box/index.h
    ${CMAKE_SOURCE_DIR}/src/box/iterator_type.h
    ${CMAKE_SOURCE_DIR}/src/box/error.h
    ${CMAKE_SOURCE_DIR}/src/box/lua/call.h
    ${CMAKE_SOURCE_DIR}/src/box/lua/tuple.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/latch.h
    ${CMAKE_SOURCE_DIR}/src/lib/core/clock.h
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
target_link_libraries(server core coll http_parser bit uri uuid swim swim_udp
                      swim_ev crypto mpstream crc32)

# Rule of thumb: if exporting a symbol from a static library, list the
# library here.
set (reexport_libraries server core misc bitset csv swim swim_udp swim_ev
     shutdown ${LUAJIT_LIBRARIES} ${MSGPUCK_LIBRARIES} ${ICU_LIBRARIES})

set (common_libraries
    ${reexport_libraries}
    ${LIBYAML_LIBRARIES}
    ${READLINE_LIBRARIES}
    ${CURL_LIBRARIES}
    ${ICONV_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${XXHASH_LIBRARIES}
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

set (common_libraries ${common_libraries} ${LIBUUID_LIBRARIES})
set (common_libraries ${common_libraries} PARENT_SCOPE)

add_subdirectory(lib)
add_subdirectory(box)

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
                ${libstatic} STREQUAL bundled-ares)
            message("We don't need to export symbols from statically linked ${libstatic}, skipped")
        else()
            message(WARNING "${libstatic} should be a static")
        endif()
    endforeach(libstatic)
    string(REPLACE ";" " " EXPORT_LIST "${EXPORT_LIST}")

    if (HAVE_OPENMP)
        # Link libgomp explicitly to make it static. Avoid linking
        # against DSO version of libgomp, which implied by -fopenmp
        set (common_libraries ${common_libraries} "libgomp.a")
        set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fno-openmp")
    endif()
endif()

if (ENABLE_LTO)
    # Exports compilation unit is entirely a hack. It references
    # symbols among static libraries and object files declaring
    # them all as functions. To avoid header dependencies. This is
    # not detected by the compilers, since they never see
    # conflicting definitions in one compilation unit. But this
    # makes LTO mad, because the linker sees all the definitions,
    # and is especially angry when a variable is declared as a
    # function. To get rid of these false positive errors the
    # exports file is not link-time optimized.
    set_source_files_properties(exports.c PROPERTIES COMPILE_FLAGS -fno-lto)
endif()

add_executable(
    tarantool main.cc exports.c
    ${LIBUTIL_FREEBSD_SRC}/flopen.c
    ${LIBUTIL_FREEBSD_SRC}/pidfile.c)

add_dependencies(tarantool build_bundled_libs)
target_link_libraries(tarantool box ${common_libraries})

if (TARGET_OS_FREEBSD AND NOT TARGET_OS_DEBIAN_FREEBSD)
    # Without rdynamic FreeBSD does not export some symbols even
    # when they are used in the final executable and their
    # addresses are accessed explicitly.
    set_target_properties(tarantool PROPERTIES LINK_FLAGS "-rdynamic")
endif()

install (TARGETS tarantool DESTINATION bin)
