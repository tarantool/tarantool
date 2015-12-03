
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
    set (luajit_cc ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1})
    # Cmake rules concerning strings and lists of strings are weird.
    #   set (foo "1 2 3") defines a string, while
    #   set (foo 1 2 3) defines a list.
    # Use separate_arguments() to turn a string into a list (splits at ws).
    # It appears that variable expansion rules are context-dependent.
    # With the current arrangement add_custom_command()
    # does the right thing. We can even handle pathnames with
    # spaces though a path with an embeded semicolon or a quotation mark
    # will most certainly wreak havok.
    #
    # This stuff is extremely fragile, proceed with caution.
    set (luajit_cflags ${CMAKE_C_FLAGS})
        separate_arguments(luajit_cflags)
    set (luajut_ldflags ${CMAKE_STATIC_LINKER_FLAGS})
        separate_arguments(luajit_ldflags)
    # We are consciously ommiting debug info in RelWithDebInfo mode
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (luajit_ccopt -O0)
        if (CC_HAS_GGDB)
            set (luajit_ccdebug -g -ggdb)
        else ()
            set (luajit_ccdebug -g)
        endif ()
        set (luajit_xcflags ${luajit_xcflags}
            -DLUA_USE_APICHECK -DLUA_USE_ASSERT)
    else ()
        set (luajit_ccopt -O2)
        set (luajit_ccdbebug "")
    endif()
    # Pass sysroot settings on OSX
    if (NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
        set (luajit_cflags ${luajit_cflags} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT})
        set (luajit_ldflags ${luajit_ldlags} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT})
    endif()
    if (ENABLE_VALGRIND)
        set (luajit_xcflags ${luajit_xcflags}
            -DLUAJIT_USE_VALGRIND -DLUAJIT_USE_SYSMALLOC)
    endif()
    set (luajit_buildoptions
        BUILDMODE=static
        CC="${luajit_cc}"
        CFLAGS="${luajit_cflags}"
        LDFLAGS="${luajit_ldflags}"
        CCOPT="${luajit_ccopt}"
        CCDEBUG="${luajit_ccdebug}"
        XCFLAGS="${luajit_xcflags}"
        Q='')
    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND $(MAKE) ${luajit_buildoptions} clean
            COMMAND $(MAKE) -C src ${luajit_buildoptions}
            DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
        )
    else()
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/third_party/luajit"
        )
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/third_party/luajit ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND $(MAKE) ${luajit_buildoptions} clean
            COMMAND $(MAKE) -C src ${luajit_buildoptions}
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${PROJECT_BINARY_DIR}/third_party/luajit
        )
    endif()
    add_custom_target(libluajit
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
    )
    add_dependencies(build_bundled_libs libluajit)
    unset (luajit_buildoptions)
    set (inc ${PROJECT_SOURCE_DIR}/third_party/luajit/src)
    install (FILES ${inc}/lua.h ${inc}/lualib.h ${inc}/lauxlib.h
        ${inc}/luaconf.h ${inc}/lua.hpp ${inc}/luajit.h
        DESTINATION ${MODULE_INCLUDEDIR})
endmacro()

#
# Building shipped luajit only if there is no
# usable system one (see cmake/luajit.cmake) or by demand.
#
if (ENABLE_BUNDLED_LUAJIT)
    luajit_build()
endif()
