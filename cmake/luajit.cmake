
#
# luajit configuration file.
#

#
# luajit defaults.
#
set (LUAJIT_DEFAULT_PREFIX "${PROJECT_BINARY_DIR}/third_party/luajit/src")
set (LUAJIT_DEFAULT_LIB "${LUAJIT_DEFAULT_PREFIX}/libluajit.a")
set (LUAJIT_TESTED 0)

macro (luajit_set_default)
    set (LUAJIT_PREFIX "${LUAJIT_DEFAULT_PREFIX}")
    set (LUAJIT_INCLUDE "${LUAJIT_DEFAULT_PREFIX}")
    set (LUAJIT_LIB "${LUAJIT_DEFAULT_LIB}")
    set (ENABLE_LUAJIT ON)
endmacro()

#
# luajit search macro.
#
macro (luajit_find isdefault)
    if (${isdefault} STREQUAL "True")
        find_path (LUAJIT_INCLUDE "lua.h")
        find_library (LUAJIT_LIB "luajit")
    else()
        find_path (LUAJIT_INCLUDE "lua.h" ${LUAJIT_PREFIX} NO_DEFAULT_PATH)
        find_library (LUAJIT_LIB "luajit" ${LUAJIT_PREFIX} NO_DEFAULT_PATH)
    endif()
endmacro()

#
# luajit testing routine
# (see cmake/luatest.cpp for description).
#
macro (luajit_test)
    file (READ "${CMAKE_SOURCE_DIR}/cmake/luatest.cpp" LUAJIT_TEST)
    set (CMAKE_REQUIRED_LIBRARIES "${LUAJIT_LIB}")
    if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        set (CMAKE_REQUIRED_LIBRARIES "-ldl ${CMAKE_REQUIRED_LIBRARIES}")
    endif()
    set (CMAKE_REQUIRED_INCLUDES "${LUAJIT_INCLUDE}")
    CHECK_CXX_SOURCE_RUNS ("${LUAJIT_TEST}" LUAJIT_RUNS)
    set (LUAJIT_TESTED "${LUAJIT_RUNS}")
    unset (LUAJIT_RUNS)
    unset (CMAKE_REQUIRED_LIBRARIES)
    unset (CMAKE_REQUIRED_INCLUDES)
endmacro()

#
# Check if there is system luajit availaible and
# it can be used with server exception's (determined by test).
#
macro (luajit_try_system)
    luajit_find(True)
    if (LUAJIT_INCLUDE AND LUAJIT_LIB)
        message (STATUS "Found system luajit.")
        luajit_test()
        if (LUAJIT_TESTED)
            message (STATUS "System luajit is suitable for use.")
        else()
            message (WARNING "System luajit is NOT suitable for use, using default.")
            # in case of system inability to use
            # system luajit, setting prefix.
	    luajit_set_default()
        endif()
    else()
        message (STATUS "Not found system luajit, using default.")
        luajit_set_default()
    endif()
endmacro()

#
# Check if there is usable luajit in specified prefix
# path.
#
macro (luajit_try_prefix)
    luajit_find(False)
    if (LUAJIT_INCLUDE AND LUAJIT_LIB)
        include_directories("${LUAJIT_INCLUDE}")
        luajit_test()
        if (LUAJIT_TESTED)
            message (STATUS "Supplied luajit is suitable for use.")
        else()
            message (FATAL_ERROR "Supplied luajit is NOT suitable for use.")
        endif()
    else()
        message (FATAL_ERROR "Couldn't find luajit in '${LUAJIT_PREFIX}'")
    endif()
endmacro()

#
# luajit options.
#
option(ENABLE_LUAJIT "Enable building of shipped luajit" OFF)
option(LUAJIT_PREFIX "luajit path" "")

if (LUAJIT_PREFIX AND ENABLE_LUAJIT)
    message (FATAL_ERROR "Only one of LUAJIT_PREFIX or ENABLE_LUAJIT "
                         "options can be specified.")
endif()

if (LUAJIT_PREFIX)
    # trying to build with specified luajit.
    luajit_try_prefix()
elseif (NOT ENABLE_LUAJIT)
    # trying to build with system luajit, macro can turn on
    # building of luajit shipped with server.
    luajit_try_system()
else()
    luajit_set_default()
endif()

message (STATUS "Luajit include: ${LUAJIT_INCLUDE}")
message (STATUS "Luajit lib: ${LUAJIT_LIB}")
