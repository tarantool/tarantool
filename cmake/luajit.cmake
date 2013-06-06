
#
# LuaJIT configuration file.
#
# A copy of LuaJIT is maintained within Tarantool
# source. It's located in third_party/luajit.
#
# Instead of this copy, Tarantool can be compiled
# with a system-wide LuaJIT, or LuaJIT at a given
# prefix. This is used when compiling Tarantool
# as part of a distribution, e.g. Debian.
#
# To explicitly request use of the bundled LuaJIT,
# add -DENABLE_BUNDLED_LUAJIT=True to CMake
# configuration flags.
# To explicitly request use of LuaJIT at a given
# prefix, use -DLUAJIT_PREFIX=/path/to/LuaJIT.
#
# These two options are incompatible with each other.
#
# If neither of the two options is given, this script
# first attempts to use the system-installed LuaJIT
# and, in case it is not present or can not be used,
# falls back to the bundled one.
#
# Adds CMake options: ENABLED_BUNDLED_LUAJIT, LUAJIT_PREFIX
# Exports CMake defines: LUAJIT_PREFIX, LUAJIT_INCLUDE, LUAJIT_LIB
# Modifies CMAKE_CFLAGS with -I${LUAJIT_INCLUDE}
#

#
# Bundled LuaJIT paths.
#
set (LUAJIT_BUNDLED_PREFIX "${PROJECT_BINARY_DIR}/third_party/luajit/src")
set (LUAJIT_BUNDLED_LIB "${LUAJIT_BUNDLED_PREFIX}/libluajit.a")

macro (luajit_use_bundled)
    set (LUAJIT_PREFIX "${LUAJIT_BUNDLED_PREFIX}")
    set (LUAJIT_INCLUDE "${PROJECT_SOURCE_DIR}/third_party/luajit/src")
    set (LUAJIT_LIB "${LUAJIT_BUNDLED_LIB}")
    set (ENABLE_BUNDLED_LUAJIT True)
endmacro()

#
# LuaJIT testing routine
# (see cmake/luatest.cpp for a description).
#
macro (luajit_test)
    file (READ "${CMAKE_SOURCE_DIR}/cmake/luatest.cpp" LUAJIT_TEST)
    set (CMAKE_REQUIRED_LIBRARIES "${LUAJIT_LIB}")
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        set (CMAKE_REQUIRED_LIBRARIES "-ldl ${CMAKE_REQUIRED_LIBRARIES}")
    endif()
    set (CMAKE_REQUIRED_INCLUDES "${LUAJIT_INCLUDE}")
    CHECK_CXX_SOURCE_RUNS ("${LUAJIT_TEST}" LUAJIT_RUNS)
    unset (LUAJIT_TEST)
    unset (CMAKE_REQUIRED_LIBRARIES)
    unset (CMAKE_REQUIRED_INCLUDES)
endmacro()

#
# Check if there is a system LuaJIT availaible and
# usable with the server (determined by a compiled test).
#
macro (luajit_try_system)
    find_path (LUAJIT_INCLUDE lj_obj.h PATH_SUFFIXES luajit-2.0 luajit)
    find_library (LUAJIT_LIB NAMES luajit luajit-5.1 PATH_SUFFIXES x86_64-linux-gnu)
    if (LUAJIT_INCLUDE AND LUAJIT_LIB)
        message (STATUS "include: ${LUAJIT_INCLUDE}, lib: ${LUAJIT_LIB}")
        message (STATUS "Found a system-wide LuaJIT.")
        luajit_test()
        if ("${LUAJIT_RUNS}" STREQUAL "1")
            message (STATUS "System-wide LuaJIT at ${LUAJIT_LIB} is suitable for use.")
        else()
            message (WARNING "System-wide LuaJIT at ${LUAJIT_LIB} is NOT suitable for use, using the bundled one.")
	        luajit_use_bundled()
        endif()
    else()
        message (FATAL_ERROR "Not found a system LuaJIT")
        #luajit_use_bundled()
    endif()
endmacro()

#
# Check if there is a usable LuaJIT at the given prefix path.
#
macro (luajit_try_prefix)
    find_path (LUAJIT_INCLUDE "lua.h" ${LUAJIT_PREFIX} NO_DEFAULT_PATH)
    find_library (LUAJIT_LIB "luajit" ${LUAJIT_PREFIX} NO_DEFAULT_PATH)
    if (LUAJIT_INCLUDE AND LUAJIT_LIB)
        include_directories("${LUAJIT_INCLUDE}")
        luajit_test()
        if (LUAJIT_RUNS)
            message (STATUS "LuaJIT at ${LUAJIT_PREFIX} is suitable for use.")
        else()
            message (FATAL_ERROR "LuaJIT at ${LUAJIT_PREFIX} is NOT suitable for use.")
        endif()
    else()
        message (FATAL_ERROR "Couldn't find LuaJIT in '${LUAJIT_PREFIX}'")
    endif()
endmacro()

#
# LuaJIT options.
#
option(ENABLE_BUNDLED_LUAJIT "Enable building of the bundled LuaJIT" ON)
option(LUAJIT_PREFIX "Build with LuaJIT at the given path" "")

if (LUAJIT_PREFIX AND ENABLE_BUNDLED_LUAJIT)
    message (FATAL_ERROR "Options LUAJIT_PREFIX and ENABLE_BUNDLED_LUAJIT "
                         "are not compatible with each other.")
endif()

if (LUAJIT_PREFIX)
    # trying to build with specified LuaJIT.
    luajit_try_prefix()
elseif (NOT ENABLE_BUNDLED_LUAJIT)
    # trying to build with system LuaJIT, macro can turn on
    # building of LuaJIT bundled with the server source.
    luajit_try_system()
else()
    luajit_use_bundled()
endif()

unset (LUAJIT_RUNS)
include_directories("${LUAJIT_INCLUDE}")

message (STATUS "Use LuaJIT includes: ${LUAJIT_INCLUDE}")
message (STATUS "Use LuaJIT library: ${LUAJIT_LIB}")

macro(luajit_build)
    set (luajit_buildoptions BUILDMODE=static)
    set (luajit_copt "")
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (luajit_buildoptions ${luajit_buildoptions} CCDEBUG=${CC_DEBUG_OPT})
        set (luajit_copt ${luajit_copt} -O1)
        set (luajit_buildoptions ${luajit_buildoptions} XCFLAGS='-DLUA_USE_APICHECK -DLUA_USE_ASSERT')
    else ()
        set (luajit_copt ${luajit_copt} -O2)
    endif()
    set (luajit_copt ${luajit_copt} -I${PROJECT_SOURCE_DIR}/libobjc)
    set (luajit_target_cc "${CMAKE_C_COMPILER} ${CMAKE_C_FLAGS}")
    # Use external unwind on all platforms.
    set (luajit_target_cc "${luajit_target_cc} -DLUAJIT_UNWIND_EXTERNAL=1")
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL ${CMAKE_HOST_SYSTEM_PROCESSOR})
        # Regular mode - use CMake compiler for building host utils.
        set (luajit_host_cc ${CMAKE_C_COMPILER})
    else()
        # Crosscompile mode - use a host CC compiler for building host utils.
        # Since CMake does not support cross compilation properly
        # we have to use system CC here.
        set (luajit_host_cc "cc")
    endif()
    if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "x86_64" AND
            ${CMAKE_SIZEOF_VOID_P} EQUAL 4)
        # The host compiler must have same pointer size as the target compiler.
        set (luajit_host_cc "${luajit_host_cc} -m32")
    endif()
    set(luajit_ldflags "${CMAKE_SHARED_LINKER_FLAGS}")
    separate_arguments(luajit_copt)
    separate_arguments(luajit_ldflags)
    separate_arguments(luajit_host_cc)
    separate_arguments(luajit_target_cc)
    set (luajit_buildoptions ${luajit_buildoptions} HOST_CC="${luajit_host_cc}" TARGET_CC="${luajit_target_cc}" CCOPT="${luajit_copt}")
    set (luajit_buildoptions ${luajit_buildoptions} Q='' LDFLAGS="${luajit_ldflags}")
    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND $(MAKE) clean
            COMMAND $(MAKE) -C src ${luajit_buildoptions} libluajit.a
            DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
        )
    else()
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/third_party/luajit"
        )
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND cp -r ${PROJECT_SOURCE_DIR}/third_party/luajit/* .
            COMMAND $(MAKE) clean
            COMMAND $(MAKE) -C src ${luajit_buildoptions} libluajit.a
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${PROJECT_BINARY_DIR}/third_party/luajit
        )
    endif()
    add_custom_target(libluajit
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
    )
    add_dependencies(build_bundled_libs libluajit)
    unset (luajit_buildoptions)
endmacro()

#
# Building shipped luajit only if there is no
# usable system one (see cmake/luajit.cmake) or by demand.
#
if (ENABLE_BUNDLED_LUAJIT)
    luajit_build()
endif()
