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
include_directories(${READLINE_INCLUDE_DIR})

# Compile src/lua/*.lua files into src/lua/*.lua.c sources
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/src/lua)
set(lua_sources)
lua_source(lua_sources lua/uuid.lua)
lua_source(lua_sources lua/session.lua)
lua_source(lua_sources lua/msgpackffi.lua)
lua_source(lua_sources lua/interactive.lua)
lua_source(lua_sources lua/load_cfg.lua)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/third_party/luafun)
lua_source(lua_sources ../third_party/luafun/fun.lua)
set(bin_sources)
bin_source(bin_sources bootstrap.snap bootstrap.h)

add_custom_target(generate_lua_sources
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
    DEPENDS ${lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

set (common_sources
     replication.cc
     memory.cc
     fiber.cc
     util.cc
     find_path.c
     sio.cc
     evio.cc
     coio.cc
     coeio.cc
     iobuf.cc
     coio_buf.cc
     pickle.cc
     coro.cc
     stat.cc
     log_io.cc
     recovery.cc
     admin.cc
     replica.cc
     iproto.cc
     iproto_constants.cc
     iproto_port.cc
     session.cc
     object.cc
     exception.cc
     ipc.cc
     errinj.cc
     errcode.c
     assoc.c
     say.cc
     fio.c
     crc32.c
     random.c
     scramble.c
     tbuf.c
     opts.c
     cfg.cc
     cpu_feature.c
     fiob.c
     tt_uuid.c
     ffisyms.cc
     lua/init.cc
     lua/fiber.cc
     lua/trigger.cc
     lua/admin.cc
     lua/info.cc
     lua/stat.cc
     lua/errinj.cc
     lua/ipc.cc
     lua/socket.cc
     lua/session.cc
     lua/cjson.cc
     lua/yaml.cc
     lua/msgpack.cc
     lua/utils.c
     ${lua_sources}
     ${bin_sources}
)

if (ENABLE_TRACE)
    set (common_sources ${common_sources} trace.c)
endif()

set_source_files_compile_flags(${common_sources})
add_library(core STATIC ${common_sources})
target_link_libraries(core pthread)

set (common_libraries core small salad misc bitset msgpuck)

list(APPEND common_libraries
    ${LIBEV_LIBRARIES}
    ${LIBEIO_LIBRARIES}
    ${LIBCORO_LIBRARIES}
    ${LIBGOPT_LIBRARIES}
    ${LIBCJSON_LIBRARIES}
    ${LIBYAML_LIBRARIES}
    ${READLINE_LIBRARIES}
    ${LUAJIT_LIB}
)

set (THREAD_LIB pthread)

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

set (common_libraries ${common_libraries} ${LIBUUID_LIBRARIES})
set (common_libraries ${common_libraries} PARENT_SCOPE)

add_subdirectory(lib)
add_subdirectory(box)
# Save CMAKE_XXX_FLAGS from this directory for config.h (used in --version)
set(TARANTOOL_C_FLAGS ${CMAKE_C_FLAGS} PARENT_SCOPE)
set(TARANTOOL_CXX_FLAGS ${CMAKE_CXX_FLAGS} PARENT_SCOPE)

add_executable(tarantool tarantool.cc)
add_dependencies(tarantool build_bundled_libs)
target_link_libraries(tarantool
    box
    ${sophia_lib}
    ${common_libraries}
    -rdynamic)

# Necessary to make LuaJIT work on Darwin, see
# http://luajit.org/install.html
if (TARGET_OS_DARWIN)
    set_target_properties(tarantool PROPERTIES
        LINK_FLAGS "-pagezero_size 10000 -image_base 100000000")
endif()
install (TARGETS tarantool DESTINATION bin)
