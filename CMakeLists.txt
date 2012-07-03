cmake_minimum_required(VERSION 2.6)

project(tarantool)

include(CheckLibraryExists)
include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(CheckSymbolExists)
include(CheckCXXSourceRuns)
include(TestBigEndian)
include(CheckFunctionExists)

find_program(ECHO echo)
find_program(XSLTPROC xsltproc)
find_program(XMLLINT xmllint)
find_program(JING jing)
find_program(LYNX lynx)
find_program(CAT cat)
find_program(GIT git)
find_program(RAGEL ragel)
find_program(CONFETTI confetti)
find_program(LD ld)
find_program(POD2MAN pod2man)

#
# This instructs the rest of the build system what product
# and what modules to produce.
#
set (TARANTOOL_PRODUCT "box")
set (TARANTOOL_MODULES "box")
set (TARANTOOL_CLIENTS "")

#
# Set default build type to Debug. This is to ease a developer's
# life. Release binaries are built by BuildBot automatically anyway.
#
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

# the order is significant: we need to know os and compiler to configure libs
include(cmake/arch.cmake)
include(cmake/os.cmake)
include(cmake/compiler.cmake)
# 
# Since we *optionally* build bundled libs, a direct build
# dependency between tarantool_box and libluajit/libobjc won't
# wor: dadd an empty custom target for this dependency instead.
# If a bundled objc or luajit is built, it is added to the
# dependency list of build_bundled_libs target.
#
add_custom_target(build_bundled_libs)
include(cmake/libobjc.cmake)
include(cmake/luajit.cmake)

check_symbol_exists(MAP_ANON sys/mman.h HAVE_MAP_ANON)
check_symbol_exists(MAP_ANONYMOUS sys/mman.h HAVE_MAP_ANONYMOUS)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(unwind.h HAVE_UNWIND_H)
check_include_file(cpuid.h HAVE_CPUID_H)

check_symbol_exists(O_DSYNC fcntl.h HAVE_O_DSYNC)
check_function_exists(fdatasync HAVE_FDATASYNC)
check_function_exists(memmem HAVE_MEMMEM)
check_function_exists(memrchr HAVE_MEMRCHR)

#
# Some versions of GNU libc define non-portable __libc_stack_end
# which we use to determine the end (or beginning, actually) of
# stack. Find whether or not it's present.
check_library_exists("" __libc_stack_end "" HAVE_LIBC_STACK_END)

#
# Enable 'make TAGS' target.
#
add_custom_target(TAGS COMMAND ctags -R --langmap=ObjectiveC:.m -e -f TAGS
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

#
# Define TARANTOOL_VERSION -- a string constant with tarantool version.
#
set (TARANTOOL_VERSION "")
execute_process (COMMAND ${GIT} describe HEAD
    OUTPUT_VARIABLE TARANTOOL_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
#
# Make sure the version is set even if building
# out of source.
#
set (CPACK_PACKAGE_VERSION_MAJOR "1")
set (CPACK_PACKAGE_VERSION_MINOR "4")
set (CPACK_PACKAGE_VERSION_PATCH "6")
if (TARANTOOL_VERSION STREQUAL "")
    set (TARANTOOL_VERSION
        "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
endif()

#
# Specify where to look for include files.
#
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_BINARY_DIR}/include")
include_directories("${PROJECT_SOURCE_DIR}/connector/c/include")
#
# Specify prefixes
#
if (NOT DEFINED CMAKE_SYSCONF_DIR)
    set (CMAKE_SYSCONF_DIR "etc")
endif()
if (NOT DEFINED CMAKE_LOCALSTATE_DIR)
    set (CMAKE_LOCALSTATE_DIR "var")
endif()
if (NOT DEFINED CMAKE_MAN_DIR)
    set (CMAKE_MAN_DIR "man")
endif()

#
# Now handle all configuration options.
#
option(ENABLE_DOC "Enable building of documentation" OFF)

option(ENABLE_CLIENT "Enable building of console client" OFF)
if (ENABLE_CLIENT)
    set (TARANTOOL_CLIENTS ${TARANTOOL_CLIENTS} "tarantool")
endif()

option(ENABLE_GCOV "Enable integration with gcov, a code coverage program" OFF)
if (ENABLE_GCOV)
    check_library_exists (gcov __gcov_flush  ""  HAVE_GCOV)
    if (NOT HAVE_GCOV)
        message (FATAL_ERROR
                 "ENABLE_GCOV option requested but gcov library is not found")
    endif()
    set (GCOV_C_FLAGS "-fprofile-arcs -ftest-coverage")
    set (GCOV_LDFLAGS "-fprofile-arcs -ftest-coverage")
endif()

option(ENABLE_TRACE "Enable debug trace of tarantool_box execution to
a file specified in TARANTOOL_TRACE environment variable" ON)

option(ENABLE_BACKTRACE "Enable output of fiber backtrace information in 'show
fiber' administrative command. Only works on x86 architectures, if compiled
with gcc. If GNU binutils and binutils-dev libraries are installed, backtrace
is output with resolved function (symbol) names. Otherwise only frame
addresses are printed." ${CMAKE_COMPILER_IS_GNUCC})

set (HAVE_BFD False)
if (ENABLE_BACKTRACE)
    if (NOT ${CMAKE_COMPILER_IS_GNUCC} OR
        NOT (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86|amd64"))
        # We only know this option to work with gcc
        # on x86 architecture.
        message (FATAL_ERROR "ENABLE_BACKTRACE option is set but the system is not x86 based (${CMAKE_SYSTEM_PROCESSOR}) or the compiler is not GNU GCC (${CMAKE_C_COMPILER}).")
    endif()
    # Use GNU bfd if present.
    check_library_exists (bfd bfd_init ""  HAVE_BFD_LIB)
    check_include_file(bfd.h HAVE_BFD_H)
    if (HAVE_BFD_LIB AND HAVE_BFD_H)
        set (HAVE_BFD True)
    endif()
endif()

option(ENABLE_STATIC "Perform static linking whenever possible." OFF)
if (ENABLE_STATIC)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")
endif()

add_subdirectory(third_party)
add_subdirectory(cfg)
add_subdirectory(connector)
add_subdirectory(src)
add_subdirectory(extra)
add_subdirectory(mod)
add_subdirectory(client)
add_subdirectory(test)
add_subdirectory(doc)

install (FILES README LICENSE doc/box-protocol.txt
         DESTINATION share/doc/tarantool)

include (cmake/tarantool_cpack.cmake)

# tarantool info summary (used in server version output)
#
set(TARANTOOL_OPTIONS "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_STATIC=${ENABLE_STATIC} -DENABLE_GCOV=${ENABLE_GCOV}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_TRACE=${ENABLE_TRACE} -DENABLE_BACKTRACE=${ENABLE_BACKTRACE}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_CLIENT=${ENABLE_CLIENT}")
set(TARANTOOL_BUILD "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_BUILD_TYPE}")
set(TARANTOOL_COMPILER ${CMAKE_C_COMPILER})

#
# Output compile-time defines into config.h. Do it at the end
# of the script to make sure all variables are set.
#
configure_file(
    "${PROJECT_SOURCE_DIR}/include/config.h.cmake"
    "${PROJECT_BINARY_DIR}/include/config.h"
    )
configure_file(
    "${PROJECT_SOURCE_DIR}/doc/tnt.ent.cmake"
    "${PROJECT_BINARY_DIR}/doc/tnt.ent"
    )

configure_file(
    "${PROJECT_SOURCE_DIR}/doc/www-data.in/download.cmake"
    "${PROJECT_BINARY_DIR}/doc/www-data.in/download"
    )
message (STATUS "")
message (STATUS "Tarantool configuration is complete:")
message (STATUS "")
message (STATUS "VERSION: ${TARANTOOL_VERSION}")
message (STATUS "BUILD: ${TARANTOOL_BUILD}")
message (STATUS "COMPILER: ${TARANTOOL_COMPILER}")
message (STATUS "CFLAGS:${CMAKE_C_FLAGS} ${core_cflags}")
message (STATUS "PREFIX: ${CMAKE_INSTALL_PREFIX}")
message (STATUS "MODULES: ${TARANTOOL_MODULES}")
message (STATUS "ENABLE_STATIC: ${ENABLE_STATIC}")
message (STATUS "ENABLE_GCOV: ${ENABLE_GCOV}")
message (STATUS "ENABLE_TRACE: ${ENABLE_TRACE}")
message (STATUS "ENABLE_BACKTRACE: ${ENABLE_BACKTRACE} (symbol resolve: ${HAVE_BFD})")
message (STATUS "ENABLE_CLIENT: ${ENABLE_CLIENT}")
message (STATUS "ENABLE_BUNDLED_LUAJIT: ${ENABLE_BUNDLED_LUAJIT}")
message (STATUS "ENABLE_DOC: ${ENABLE_DOC}")
message (STATUS "")
message (STATUS "To view or modify configuration results, check out CMakeCache.txt.")
message (STATUS "")
