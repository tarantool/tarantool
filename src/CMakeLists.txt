#
# Set compile flags for entire src/ directory
#
enable_tnt_compile_flags()

if (NOT TARGET_OS_DARWIN)
    add_subdirectory(module)
endif()

include_directories(${LIBEV_INCLUDE_DIR})
include_directories(${LIBEIO_INCLUDE_DIR})
include_directories(${LIBCORO_INCLUDE_DIR})
include_directories(${LIBGOPT_INCLUDE_DIR})

# Require pthread globally if compiling with GCC
if (CMAKE_COMPILER_IS_GNUCC)
    add_compile_flags("C;CXX" "-pthread")
endif()

#
# Build admin.cc from admin.rl, but only if admin.rl was changed.
# The same applies to memcached.cc/memcached.rl.
# We track admin.cc and memcached.cc in revision control, and thus do not
# require engineers who do not modify .rl files to have Ragel
# installed.
#
add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/src/admin.cc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 src/admin.rl -o src/admin.cc
    DEPENDS ${CMAKE_SOURCE_DIR}/src/admin.rl)

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/src/memcached-grammar.cc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 src/memcached-grammar.rl
                     -o src/memcached-grammar.cc
    DEPENDS ${CMAKE_SOURCE_DIR}/src/memcached-grammar.rl)

add_custom_target(generate_admin_cc DEPENDS ${CMAKE_SOURCE_DIR}/src/admin.cc)
add_custom_target(generate_memcached_grammar_cc DEPENDS
    ${CMAKE_SOURCE_DIR}/src/memcached-grammar.cc)

# do not randomly try to re-generate admin.cc or memcached-grammar.cc
# after a fresh checkout/branch switch.
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/src/admin.cc
    ${CMAKE_SOURCE_DIR}/src/memcached-grammar.cc)

set_source_files_properties(${CMAKE_SOURCE_DIR}/src/memcached-grammar.cc
    PROPERTIES HEADER_FILE_ONLY true)

set_source_files_properties(memcached.cc
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

#
# # Do not clean admin.cc, memcached.cc or other
# generated files in 'make clean' -- they are under
# revision control.
#
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM true)

# Compile src/lua/*.lua files into src/lua/*.lua.c sources
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/lua)
set(lua_sources)
lua_source(lua_sources lua/uuid.lua)
lua_source(lua_sources lua/session.lua)

add_custom_target(generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

#
# Used by modules.
#
set (recompiled_sources
     ${CMAKE_SOURCE_DIR}/src/tarantool.cc
     ${CMAKE_SOURCE_DIR}/src/opts.c
     ${CMAKE_SOURCE_DIR}/src/lua/init.cc
     ${CMAKE_SOURCE_DIR}/src/say.cc
     ${CMAKE_SOURCE_DIR}/src/assoc.cc
     ${CMAKE_SOURCE_DIR}/src/replication.cc
     ${CMAKE_SOURCE_DIR}/src/memcached.cc
     ${CMAKE_SOURCE_DIR}/src/fiber.cc)

set (common_sources
     tbuf.c
     palloc.cc
     util.cc
     sio.cc
     evio.cc
     coio.cc
     coeio.cc
     iobuf.cc
     coio_buf.cc
     salloc.cc
     pickle.cc
     coro.cc
     stat.cc
     log_io.cc
     recovery.cc
     admin.cc
     cpu_feature.c
     replica.cc
     iproto.cc
     iproto_port.cc
     session.cc
     object.cc
     exception.cc
     errcode.c
     errinj.cc
     fio.c
     crc32.c
     rope.c
     ipc.cc
     lua/info.cc
     lua/stat.cc
     lua/slab.cc
     lua/lua_ipc.cc
     lua/lua_socket.cc
     lua/session.cc
     lua/cjson.cc
     fiob.c
     ${lua_sources}
)

if (ENABLE_TRACE)
    set (common_sources ${common_sources} trace.c)
endif()

set_source_files_properties(sio.cc evio.cc replica.cc PROPERTIES COMPILE_FLAGS "-Wno-deprecated")
set_source_files_compile_flags(${common_sources})
add_library(core STATIC ${common_sources})
add_dependencies(core generate_headers)

set (common_libraries cfg core)

list(APPEND common_libraries
    ${LIBEV_LIBRARIES}
    ${LIBEIO_LIBRARIES}
    ${LIBCORO_LIBRARIES}
    ${LIBGOPT_LIBRARIES}
    ${LIBCJSON_LIBRARIES}
    ${LUAJIT_LIB}
    misc
)

set (THREAD_LIB pthread)
if (ENABLE_STATIC)
    set (THREAD_LIB -Wl,--whole-archive pthread -Wl,--no-whole-archive)
endif()

set (common_libraries ${common_libraries} ${THREAD_LIB})

if (TARGET_OS_LINUX OR TARGET_OS_DEBIAN_FREEBSD)
    set (common_libraries ${common_libraries} dl)
endif()

if (TARGET_OS_FREEBSD AND NOT TARGET_OS_DEBIAN_FREEBSD)
    find_library (INTL intl)
    if (NOT INTL)
        message(FATAL_ERROR "intl library not found")
    else()
        set (common_libraries ${common_libraries} ${INTL})
    endif()
endif()

if (ENABLE_BACKTRACE AND HAVE_BFD)
    find_package (ZLIB)
    set (common_libraries ${common_libraries} bfd ${ZLIB_LIBRARIES})
    if (NOT TARGET_OS_DARWIN)
        set (common_libraries ${common_libraries} iberty)
    endif()
    if (TARGET_OS_FREEBSD AND NOT TARGET_OS_DEBIAN_FREEBSD)
        set (common_libraries ${common_libraries} iconv)
    endif()
endif()

set (common_libraries ${common_libraries} PARENT_SCOPE)

add_subdirectory(lib)
# Save CMAKE_XXX_FLAGS from this directory for config.h (used in --version)
set(TARANTOOL_C_FLAGS ${CMAKE_C_FLAGS} PARENT_SCOPE)
set(TARANTOOL_CXX_FLAGS ${CMAKE_CXX_FLAGS} PARENT_SCOPE)

function(tarantool_module mod)
    set (module_sources ${ARGN})
    set(cfg_c_flags "-Wno-unused -Wno-unused-parameter")
    if (CMAKE_COMPILER_IS_CLANG)
        set(cfg_c_flags "${cfg_c_flags} -Wno-semicolon-before-method-body")
    endif()
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c
        PROPERTIES COMPILE_FLAGS ${cfg_c_flags}
        GENERATED True)
    unset(cfg_c_flags)
    add_executable(tarantool_${mod}
        ${module_sources}
        ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c)

    set_source_files_properties(${recompiled_sources}
        PROPERTIES OBJECT_DEPENDS
        ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.h)

    set_source_files_compile_flags(
        ${recompiled_sources} ${module_sources})

    add_library(lt${mod} STATIC ${recompiled_sources})
    set_target_properties(lt${mod} PROPERTIES COMPILE_FLAGS
        "-DTARANTOOL_CONFIG='<cfg/tarantool_${mod}_cfg.h>'")
    add_dependencies(lt${mod} generate_headers generate_admin_cc generate_memcached_grammar_cc build_bundled_libs)

    target_link_libraries(tarantool_${mod} lt${mod} ${common_libraries} -rdynamic)

    if (ENABLE_STATIC)
        set_target_properties(tarantool_${mod} PROPERTIES
            LINK_SEARCH_END_STATIC ON)
        set (module_link_flags "${module_link_flags} -static")
    endif()

    if (module_link_flags)
        set_target_properties(tarantool_${mod} PROPERTIES
            LINK_FLAGS ${module_link_flags})
    endif()
    install (TARGETS tarantool_${mod} DESTINATION bin)
endfunction()

foreach (module ${TARANTOOL_MODULES})
add_subdirectory(${module})
endforeach()
