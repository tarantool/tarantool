#
# Set compile flags for entire src/ directory
#
enable_tnt_compile_flags()

include_directories(${LIBEV_INCLUDE_DIR})
include_directories(${LIBEIO_INCLUDE_DIR})
include_directories(${LIBCORO_INCLUDE_DIR})
include_directories(${LIBGOPT_INCLUDE_DIR})
if (ENABLE_BUNDLED_LIBOBJC)
    include_directories(${LIBOBJC_INCLUDE_DIR})
endif()

# Require pthread globally if compiling with GCC
if (CMAKE_COMPILER_IS_GNUCC)
    add_compile_flags("C;CXX" "-pthread")
endif()

#
# Build admin.m from admin.rl, but only if admin.rl was changed.
# The same applies to memcached.m/memcached.rl.
# We track admin.m and memcached.m in revision control, and thus do not
# require engineers who do not modify .rl files to have Ragel
# installed.
#
add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/src/admin.m
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 src/admin.rl -o src/admin.m
    DEPENDS ${CMAKE_SOURCE_DIR}/src/admin.rl)

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/src/memcached-grammar.m
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${RAGEL} -G2 src/memcached-grammar.rl
                     -o src/memcached-grammar.m
    DEPENDS ${CMAKE_SOURCE_DIR}/src/memcached-grammar.rl)

add_custom_target(generate_admin_m DEPENDS ${CMAKE_SOURCE_DIR}/src/admin.m)
add_custom_target(generate_memcached_grammar_m DEPENDS
    ${CMAKE_SOURCE_DIR}/src/memcached-grammar.m)

# do not randomly try to re-generate admin.m or memcached-grammar.m
# after a fresh checkout/branch switch.
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/src/admin.m
    ${CMAKE_SOURCE_DIR}/src/memcached-grammar.m)

set_source_files_properties(${CMAKE_SOURCE_DIR}/src/memcached-grammar.m
    PROPERTIES HEADER_FILE_ONLY true)

set_source_files_properties(memcached.m
    PROPERTIES COMPILE_FLAGS "-Wno-uninitialized")

#
# Do not clean admin.m, memcached.m or other
# generated files in 'make clean' -- they are under
# revision control.
#
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM true)

#
# Used by modules.
#
set (recompiled_sources
     ${CMAKE_SOURCE_DIR}/src/tarantool.m
     ${CMAKE_SOURCE_DIR}/src/lua/init.m
     ${CMAKE_SOURCE_DIR}/src/say.m
     ${CMAKE_SOURCE_DIR}/src/assoc.m
     ${CMAKE_SOURCE_DIR}/src/replication.m
     ${CMAKE_SOURCE_DIR}/src/memcached.m
     ${CMAKE_SOURCE_DIR}/src/fiber.m)

set (common_sources
     tbuf.m
     palloc.m
     util.m
     sio.m
     evio.m
     coio.m
     coeio.m
     iobuf.m
     coio_buf.m
     salloc.m
     pickle.m
     coro.m
     stat.m
     log_io.m
     recovery.m
     admin.m
     cpu_feature.m
     replica.m
     iproto.m
     session.m
     object.m
     exception.m
     errcode.c
     errinj.m
     fio.c
     crc32.c
     rope.c
     ipc.m
     lua/info.m
     lua/stat.m
     lua/slab.m
     lua/uuid.m
     lua/lua_ipc.m
     lua/lua_socket.m
     lua/session.m
)

if (ENABLE_TRACE)
    set (common_sources ${common_sources} trace.m)
endif()

set_source_files_compile_flags(${common_sources})
add_library(core STATIC ${common_sources})
add_dependencies(core generate_headers)

set (common_libraries cfg core)

list(APPEND common_libraries
    ${LIBEV_LIBRARIES}
    ${LIBEIO_LIBRARIES}
    ${LIBCORO_LIBRARIES}
    ${LIBGOPT_LIBRARIES}
    ${LUAJIT_LIB}
    ${LIBOBJC_LIBRARIES}
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

if (TARGET_OS_FREEBSD)
    set (common_libraries ${common_libraries} intl)
endif()

if (ENABLE_BACKTRACE AND HAVE_BFD)
    set (common_libraries ${common_libraries} bfd)
    if (NOT TARGET_OS_DARWIN)
        set (common_libraries ${common_libraries} iberty)
    endif()
    if (ENABLE_STATIC OR TARGET_OS_FREEBSD)
        include (FindZLIB)
        set (common_libraries ${common_libraries} ${ZLIB_LIBRARIES})
    endif()
    if (TARGET_OS_FREEBSD)
        set (common_libraries ${common_libraries} iconv intl)
    endif()
endif()

set (common_libraries ${common_libraries} PARENT_SCOPE)

add_subdirectory(lib)
# Save CMAKE_XXX_FLAGS from this directory for config.h (used in --version)
set(TARANTOOL_C_FLAGS ${CMAKE_C_FLAGS} PARENT_SCOPE)
set(TARANTOOL_CXX_FLAGS ${CMAKE_CXX_FLAGS} PARENT_SCOPE)
set(TARANTOOL_OBJC_FLAGS ${CMAKE_OBJC_FLAGS} PARENT_SCOPE)
set(TARANTOOL_OBJCXX_FLAGS ${CMAKE_OBJCXX_FLAGS} PARENT_SCOPE)

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
    add_dependencies(lt${mod} generate_headers generate_admin_m generate_memcached_grammar_m build_bundled_libs)

    target_link_libraries(tarantool_${mod} lt${mod} ${common_libraries})

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
