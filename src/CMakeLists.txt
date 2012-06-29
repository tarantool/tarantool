#
# libev library
#
add_library(ev tarantool_ev.c)

if (CMAKE_COMPILER_IS_GNUCC)
    check_c_compiler_flag ("-Wno-unused-result" gcc_has_wno_unused_result)
    if (gcc_has_wno_unused_result)
        set_source_files_properties(tarantool_ev.c
                PROPERTIES COMPILE_FLAGS "-Wno-unused-result")
    endif()
elseif (CMAKE_COMPILER_IS_CLANG)
    set_source_files_properties(tarantool_ev.c
            PROPERTIES COMPILE_FLAGS "-Wno-unused-value -Wno-comment -Wno-bitwise-op-parentheses")
endif()

if (TARGET_OS_LINUX)
#
# Enable Linux-specific event notification API (man inotify)
  set_target_properties(ev PROPERTIES COMPILE_FLAGS "-DEV_USE_INOTIFY")
elseif (TARGET_OS_FREEBSD)
#
# On FreeBSD build libev loop on top of
  set_target_properties(ev PROPERTIES COMPILE_FLAGS "-DEV_USE_KQUEUE")
endif()

#
# libev uses ceil and floor from the standard math library
#

#
# libev depends on librt under kFreeBSD
if (TARGET_OS_DEBIAN_FREEBSD)
  set (ev_libs m rt)
else()
  set (ev_libs m)
endif()

target_link_libraries(ev ${ev_libs})

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

add_custom_target(generate_admin_m DEPENDS ${CMAKE_SOURCE_DIR}/src/admin.m)
# do not randomly try to re-generate admin.m after a fresh checkout
execute_process(COMMAND ${CMAKE_COMMAND} -E touch_nocreate
    ${CMAKE_SOURCE_DIR}/src/admin.m)

#
# Do not clean admin.m, memcached.m or other
# generated files in 'make clean' -- they are under
# revision control.
#
set_property(DIRECTORY PROPERTY CLEAN_NO_CUSTOM 1)

#
# Used by modules.
#
set (recompiled_sources
     ${CMAKE_SOURCE_DIR}/src/tarantool.m
     ${CMAKE_SOURCE_DIR}/src/tarantool_lua.m
     ${CMAKE_SOURCE_DIR}/src/say.m
     ${CMAKE_SOURCE_DIR}/src/assoc.m
     ${CMAKE_SOURCE_DIR}/src/admin.m
     ${CMAKE_SOURCE_DIR}/src/replication.m
     ${CMAKE_SOURCE_DIR}/src/fiber.m PARENT_SCOPE)

set (common_sources tbuf.m palloc.m util.m
     salloc.m pickle.m coro.m stat.m log_io.m recovery.m cpu_feature.m
     replica.m iproto.m object.m exception.m errcode.c errinj.m latch.m
     nio.c crc32.c)

if (ENABLE_TRACE)
    set (common_sources ${common_sources} trace.m)
endif()

add_library(core STATIC ${common_sources})
add_dependencies(core generate_headers)
set_target_properties(core PROPERTIES COMPILE_FLAGS "${core_cflags}")

set (common_libraries cfg core ev coro gopt misc)

set (THREAD_LIB pthread)
if (ENABLE_STATIC)
    set (THREAD_LIB -Wl,--whole-archive pthread -Wl,--no-whole-archive)
endif()

set (LIBOBJC_LIB "${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a")
set (common_libraries ${common_libraries} ${LUAJIT_LIB} ${LIBOBJC_LIB} ${THREAD_LIB})

if (TARGET_OS_LINUX)
    set (common_libraries ${common_libraries} dl)
endif()

if (TARGET_OS_DEBIAN_FREEBSD)
    set (common_libraries ${common_libraries} dl)
endif()

if (ENABLE_GCOV)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GCOV_C_FLAGS}")
    set (LDFLAGS "${LDFLAGS} ${GCOV_LDFLAGS}")
    set (common_libraries ${common_libraries} gcov)
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
endif()

set (common_libraries ${common_libraries} PARENT_SCOPE) 
