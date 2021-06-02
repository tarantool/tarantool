#
# LuaJIT configuration file.
#
# A copy of LuaJIT is maintained within Tarantool source tree.
# It's located in third_party/luajit.
#
# LUAJIT_INCLUDE_DIRS
# LUAJIT_LIBRARIES
#
# This stuff is extremely fragile, proceed with caution.

macro(TestAndAppendFLag flags flag)
    string(REGEX REPLACE "-" "_" TESTFLAG ${flag})
    string(TOUPPER ${TESTFLAG} TESTFLAG)
    # XXX: can't use string(PREPEND ...) on ancient versions.
    set(TESTFLAG "CC_HAS${TESTFLAG}")
    if(${${TESTFLAG}})
        set(${flags} "${${flags}} ${flag}")
    endif()
endmacro()

# Preserve the current CFLAGS and to not spoil the original ones with LuaJIT
# specific flags and defines.
set(CMAKE_C_FLAGS_BCKP ${CMAKE_C_FLAGS})

TestAndAppendFLag(CMAKE_C_FLAGS -Wno-parentheses-equality)
TestAndAppendFLag(CMAKE_C_FLAGS -Wno-tautological-compare)
TestAndAppendFLag(CMAKE_C_FLAGS -Wno-misleading-indentation)
TestAndAppendFLag(CMAKE_C_FLAGS -Wno-varargs)
TestAndAppendFLag(CMAKE_C_FLAGS -Wno-implicit-fallthrough)

set(BUILDMODE static CACHE STRING
    "Build mode: build only static lib" FORCE)
set(LUAJIT_ENABLE_GC64 ${LUAJIT_ENABLE_GC64} CACHE BOOL
    "GC64 mode for x64" FORCE)
set(LUAJIT_SMART_STRINGS ON CACHE BOOL
    "Harder string hashing function" FORCE)
set(LUAJIT_TEST_BINARY $<TARGET_FILE:tarantool> CACHE STRING
    "Lua implementation to be used for tests (tarantool)" FORCE)
set(LUAJIT_USE_TEST OFF CACHE BOOL
    "Generate <test> target" FORCE)

# XXX: There is <strict> module enabled by default in Tarantool
# built in Debug, so we need to tweak LuaJIT testing environment.
# XXX: Also, this script "unloads" internal Tarantool's modules
# and remove globals conflicting with LuaJIT test suites.
set(LUAJIT_TEST_INIT "${PROJECT_SOURCE_DIR}/test/luajit-test-init.lua"
    CACHE STRING "Lua code need to be run before tests are started" FORCE)

# Enable internal LuaJIT assertions for Tarantool Debug build.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LUA_USE_APICHECK ON CACHE BOOL
        "Assertions for the Lua/C API" FORCE)
    set(LUA_USE_ASSERT ON CACHE BOOL
        "Assertions for the whole LuaJIT VM" FORCE)
endif()

# Valgrind can be used only with the system allocator. For more
# info, see LuaJIT root CMakeLists.txt.
if(ENABLE_VALGRIND)
    set(LUAJIT_USE_SYSMALLOC ON CACHE BOOL
        "System provided memory allocator (realloc)" FORCE)
    set(LUAJIT_USE_VALGRIND ON CACHE BOOL
        "Valgrind support" FORCE)
endif()

# FIXME: ASAN support is badly implemented in LuaJIT and there is
# not a specific build options for this. At the same time there
# are several places wrapped with LUAJIT_USE_ASAN define.
# Just enable it here if needed and patiently wait until ASAN
# support is implemented properly in LuaJIT.
if(ENABLE_ASAN)
    add_definitions(-DLUAJIT_USE_ASAN=1)
endif()

if(TARGET_OS_DARWIN AND NOT LUAJIT_ENABLE_GC64)
    # XXX: This is not the best idea to build LuaJIT on MacOS
    # with GC64 disabled. But nobody will stop you from this.
    # You are warned. For more info, see the following issue.
    # https://github.com/tarantool/tarantool/issues/2643
    message(WARNING "LUAJIT_ENABLE_GC64 is disabled for MacOS. "
                    "If one wants to know why this is not a good idea, "
                    "see https://github.com/tarantool/tarantool/issues/2643.")
    # XXX: Necessary to make LuaJIT (and hence Tarantool) work on
    # Darwin/x86_64, see the following links for more info:
    # * http://luajit.org/install.html#embed
    # * https://github.com/tarantool/luajit/blob/789820a/cmake/SetTargetFlags.cmake
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(CMAKE_EXE_LINKER_FLAGS
            "${CMAKE_EXE_LINKER_FLAGS} -pagezero_size 10000 -image_base 100000000")
    endif()
endif()

# Define the locations for LuaJIT sources and artefacts.
set(LUAJIT_SOURCE_ROOT ${PROJECT_SOURCE_DIR}/third_party/luajit)
set(LUAJIT_BINARY_ROOT ${PROJECT_BINARY_DIR}/third_party/luajit)

add_subdirectory(${LUAJIT_SOURCE_ROOT} ${LUAJIT_BINARY_ROOT} EXCLUDE_FROM_ALL)

set(LUAJIT_PREFIX ${LUAJIT_BINARY_ROOT}/src)
set(LUAJIT_INCLUDE_DIRS ${LUAJIT_PREFIX})
set(LUAJIT_LIBRARIES ${LUAJIT_PREFIX}/libluajit.a)

add_dependencies(build_bundled_libs libluajit)

install(
    FILES
        ${LUAJIT_SOURCE_ROOT}/src/lua.h
        ${LUAJIT_SOURCE_ROOT}/src/lualib.h
        ${LUAJIT_SOURCE_ROOT}/src/lauxlib.h
        ${LUAJIT_SOURCE_ROOT}/src/luaconf.h
        ${LUAJIT_SOURCE_ROOT}/src/lua.hpp
        ${LUAJIT_SOURCE_ROOT}/src/luajit.h
        ${LUAJIT_SOURCE_ROOT}/src/lmisclib.h
    DESTINATION ${MODULE_INCLUDEDIR}
)

# XXX: Since Tarantool use LuaJIT internals to implement all
# required interfaces, several defines and flags need to be set
# for Tarantool too.
# FIXME: Hope everything below will have gone in a future.

# Include LuaJIT source directory to use the internal headers.
include_directories(${LUAJIT_SOURCE_ROOT}/src)

# Since LUAJIT_SMART_STRINGS is enabled for LuaJIT bundle, it
# should be unconditionally enabled for Tarantool too. Otherwise,
# all modules using LuaJIT internal headers are misaligned.
add_definitions(-DLUAJIT_SMART_STRINGS=1)
# The same is done for LUAJIT_ENABLE_GC64 but under the condition.
if(LUAJIT_ENABLE_GC64)
    add_definitions(-DLUAJIT_ENABLE_GC64)
endif()
# XXX: Tarantool can't be built with FFI machinery disabled, since
# there are lots of internals implemented with it. Hence, forbid
# user to disable FFI at configuration phase.
if(LUAJIT_DISABLE_FFI)
    message(FATAL_ERROR "Tarantool requires LuaJIT FFI machinery to be enabled")
endif()

# Restore the preserved CFLAGS.
set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS_BCKP})
