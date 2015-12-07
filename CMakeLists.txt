cmake_minimum_required(VERSION 2.8)

project(tarantool C CXX)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_INCLUDE_PATH})

include(CheckLibraryExists)
include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(CheckSymbolExists)
include(CheckCSourceRuns)
include(CheckCXXSourceRuns)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(TestBigEndian)
include(CheckFunctionExists)
include(FindOptionalPackage)

find_program(ECHO echo)
find_program(XSLTPROC xsltproc)
find_program(XMLLINT xmllint)
find_program(JING jing)
find_program(LYNX lynx)
find_program(CAT cat)
find_program(GIT git)
find_program(LD ld)
find_program(CTAGS ctags)

# Define PACKAGE macro in tarantool/config.h
set (PACKAGE "Tarantool")

#
# Set default build type to Debug. This is to ease a developer's
# life. Release binaries are built by BuildBot automatically anyway.
#
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

# Define GNU standard installation directories
include(GNUInstallDirs)

include(cmake/utils.cmake)
include(cmake/pod2man.cmake)
# the order is significant: we need to know os and compiler to configure libs
include(cmake/arch.cmake)
include(cmake/os.cmake)
include(cmake/compiler.cmake)
include(cmake/simd.cmake)
include(cmake/atomic.cmake)
include(cmake/profile.cmake)
include(cmake/FindReadline.cmake)
include(cmake/FindSphinx.cmake)
include(cmake/systemd.cmake)
include(cmake/module.cmake)
include(cmake/thread.cmake)

if (NOT READLINE_FOUND)
    message(FATAL_ERROR "readline library not found.")
endif()

option(ENABLE_VALGRIND "Enable integration with valgrind, a memory analyzing tool" OFF)

check_symbol_exists(MAP_ANON sys/mman.h HAVE_MAP_ANON)
check_symbol_exists(MAP_ANONYMOUS sys/mman.h HAVE_MAP_ANONYMOUS)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(unwind.h HAVE_UNWIND_H)
check_include_file(cpuid.h HAVE_CPUID_H)
check_include_file(sys/prctl.h HAVE_PRCTL_H)

check_symbol_exists(O_DSYNC fcntl.h HAVE_O_DSYNC)
check_symbol_exists(fdatasync unistd.h HAVE_FDATASYNC)
check_symbol_exists(pthread_yield pthread.h HAVE_PTHREAD_YIELD)
check_symbol_exists(sched_yield sched.h HAVE_SCHED_YIELD)

check_function_exists(memmem HAVE_MEMMEM)
check_function_exists(memrchr HAVE_MEMRCHR)
check_function_exists(sendfile HAVE_SENDFILE)
if (HAVE_SENDFILE)
    if (TARGET_OS_LINUX)
        set(HAVE_SENDFILE_LINUX 1)
    else(HAVE_SENDFILE)
        set(HAVE_SENDFILE_BSD 1)
    endif()
endif()
check_function_exists(open_memstream HAVE_OPEN_MEMSTREAM)
check_function_exists(fmemopen HAVE_FMEMOPEN)
check_function_exists(funopen HAVE_FUNOPEN)
check_function_exists(fopencookie HAVE_FOPENCOOKIE)
check_function_exists(uuidgen HAVE_UUIDGEN)
#
# clock_gettime() is not defined on Mac OS X,
# or own (slow) implementation is in third_party/clock_gettime.c
#
if (TARGET_OS_LINUX)
    check_library_exists(rt clock_gettime "" HAVE_CLOCK_GETTIME)
else()
    check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
endif()

# Checks for libev
include(CheckStructHasMember)
check_struct_has_member("struct stat" "st_mtim" "sys/stat.h"
    HAVE_STRUCT_STAT_ST_MTIM)
check_struct_has_member("struct stat" "st_mtimensec" "sys/stat.h"
    HAVE_STRUCT_STAT_ST_MTIMENSEC)

#
# Some versions of GNU libc define non-portable __libc_stack_end
# which we use to determine the end (or beginning, actually) of
# stack. Find whether or not it's present.
check_library_exists("" __libc_stack_end "" HAVE_LIBC_STACK_END)

check_function_exists(setproctitle HAVE_SETPROCTITLE)
check_function_exists(setprogname HAVE_SETPROGNAME)
check_function_exists(getprogname HAVE_GETPROGNAME)

#
# Enable 'make tags' target.
#
add_custom_target(tags COMMAND ${CTAGS} -R -f tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_custom_target(ctags DEPENDS tags)

#
# Define PACKAGE_VERSION -- a string constant with tarantool version.
#
set (CPACK_PACKAGE_VERSION_MAJOR "1")
set (CPACK_PACKAGE_VERSION_MINOR "7")
set (CPACK_PACKAGE_VERSION_PATCH "0")

set (PACKAGE_VERSION "")

# Get git version only if source directory has .git repository, this
# avoids git to search .git repository in parent
# directories.
#
if (EXISTS "${CMAKE_SOURCE_DIR}/.git")
	execute_process (COMMAND ${GIT} describe HEAD
		OUTPUT_VARIABLE PACKAGE_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    if (PACKAGE_VERSION STREQUAL "")
        message (FATAL_ERROR "Failed to retrive git version.")
    endif()
endif()

set (VERSION_FILE "${CMAKE_SOURCE_DIR}/VERSION")

# Update version file or use it when there is no git
# repository within sources (source tarballs).
#
if (PACKAGE_VERSION STREQUAL "")
    if (NOT EXISTS "${VERSION_FILE}")
        message (FATAL_ERROR "Version file ${VERSION_FILE} does not exists.")
    endif()
    message (WARNING "Using version from version file ${VERSION_FILE}")
	execute_process (COMMAND ${CAT} ${VERSION_FILE}
		OUTPUT_VARIABLE PACKAGE_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (PACKAGE_VERSION STREQUAL "")
        message (FATAL_ERROR "Unable to retrive version from git or ${VERSION_FILE} file.")
    endif()
else()
	file(WRITE ${VERSION_FILE} "${PACKAGE_VERSION}\n")
endif()

#
# Specify where to look for include files.
#
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/lib)
include_directories(${PROJECT_SOURCE_DIR}/src/lib/small)
include_directories(${PROJECT_SOURCE_DIR})

#
# Specify Tarantool modules prefixes
#

set(MODULE_SUFFIX "tarantool")
set(MODULE_LIBDIR "${CMAKE_INSTALL_LIBDIR}/${MODULE_SUFFIX}")
set(MODULE_LUADIR "${CMAKE_INSTALL_DATADIR}/${MODULE_SUFFIX}")
set(MODULE_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_SUFFIX}")

foreach(dir
        LIBDIR
        LUADIR
        INCLUDEDIR
        )
    if(NOT IS_ABSOLUTE ${MODULE_${dir}})
        set(MODULE_FULL_${dir} "${CMAKE_INSTALL_PREFIX}/${MODULE_${dir}}")
    else()
        set(MODULE_FULL_${dir} "${MODULE_${dir}}")
    endif()
endforeach()

message(STATUS "Module sources:   ${MODULE_FULL_LUADIR}")
message(STATUS "Module libraries: ${MODULE_FULL_LIBDIR}")
message(STATUS "Module includes:  ${MODULE_FULL_INCLUDEDIR}")

list(APPEND MODULE_LUAPATH "${MODULE_FULL_LUADIR}/?.lua")
list(APPEND MODULE_LUAPATH "${MODULE_FULL_LUADIR}/?/init.lua")
list(APPEND MODULE_LIBPATH "${MODULE_FULL_LIBDIR}/?${CMAKE_SHARED_MODULE_SUFFIX}")

message(STATUS "Module package.path: ${MODULE_LUAPATH}")
message(STATUS "Module package.cpath: ${MODULE_LIBPATH}")

#
# Now handle all configuration options.
#
option(ENABLE_DOC "Enable building of documentation" OFF)

option(ENABLE_TRACE "Enable debug trace of tarantool_box execution to
a file specified in TARANTOOL_TRACE environment variable" ON)

option(ENABLE_BACKTRACE "Enable output of fiber backtrace information in 'show
fiber' administrative command. Only works on x86 architectures, if compiled
with gcc. If GNU binutils and binutils-dev libraries are installed, backtrace
is output with resolved function (symbol) names. Otherwise only frame
addresses are printed." ${CMAKE_COMPILER_IS_GNUCC})

set (HAVE_BFD False)
if (ENABLE_BACKTRACE)
    if (NOT ${CMAKE_COMPILER_IS_GNUCC})
        # We only know this option to work with gcc
        message (FATAL_ERROR "ENABLE_BACKTRACE option is set but the system is not x86 based (${CMAKE_SYSTEM_PROCESSOR}) or the compiler is not GNU GCC (${CMAKE_C_COMPILER}).")
    endif()
    # Use GNU bfd if present.
    check_library_exists (bfd bfd_init ""  HAVE_BFD_LIB)
    check_library_exists (iberty cplus_demangle "" HAVE_IBERTY_LIB)
    set(CMAKE_REQUIRED_DEFINITIONS -DPACKAGE=${PACKAGE} -DPACKAGE_VERSION=${PACKAGE_VERSION})
    check_include_file(bfd.h HAVE_BFD_H)
    set(CMAKE_REQUIRED_DEFINITIONS)
    if (HAVE_BFD_LIB AND HAVE_BFD_H AND HAVE_IBERTY_LIB)
        set (HAVE_BFD True)
    endif()
endif()

##
## Third-Party libraries
##

#
# Since we *optionally* build bundled libs, a direct build
# dependency between tarantool_box and libluajit/libobjc won't
# work: add an empty custom target for this dependency instead.
# If a bundled objc or luajit is built, it is added to the
# dependency list of build_bundled_libs target.
#

add_custom_target(build_bundled_libs)

#
# LibLUAJIT
#
include(luajit)


#
# LibEV
#

#
# Currently our code uses libev with #define EV_MULTIPLICITY 0.
# This option means that libev has a global variable with
# struct ev_loop data.
# Such design is not compatible with the dynamic version of libev
# provided by distros.
set(ENABLE_BUNDLED_LIBEV ON)
include(BuildLibEV)
libev_build()
add_dependencies(build_bundled_libs ev)

#
# LibEIO
#
option(ENABLE_BUNDLED_LIBEIO "Enable building of the bundled libeio" ON)

if (ENABLE_BUNDLED_LIBEIO)
    include(BuildLibEIO)
    libeio_build()
    add_dependencies(build_bundled_libs eio)
else()
    set(LIBEIO_FIND_REQUIRED ON)
    find_package(LibEIO)
endif()


#
# LibCORO
#

#
# Tarantool uses 'coro' (coroutines) library to implement
# cooperative multi-tasking. Since coro.h is included
# universally, define the underlying implementation switch
# in the top level CMakeLists.txt, to ensure a consistent
# header file layout across the entire project.
#
set(ENABLE_BUNDLED_LIBCORO ON)
include(BuildLibCORO)
libcoro_build()
add_dependencies(build_bundled_libs coro)

if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "amd64")
    add_definitions("-DCORO_ASM")
elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
    add_definitions("-DCORO_ASM")
else()
    add_definitions("-DCORO_SJLJ")
endif()


#
# LibGOPT
#

include(BuildLibGOPT)
libgopt_build()
add_dependencies(build_bundled_libs gopt)

#
# LibCJSON
#

include(BuildLibCJSON)
libcjson_build()
add_dependencies(build_bundled_libs cjson)

#
# LibYAML
#

include(BuildLibYAML)
libyaml_build()
add_dependencies(build_bundled_libs yaml)

#
# Third-Party misc
#

include(BuildMisc)
libmisc_build()
add_dependencies(build_bundled_libs misc)

include(BuildSophia)
sophia_build()

option(ENABLE_RPM "Enable install of a RPM specific files" OFF)

# cpack config. called package.cmake to avoid
# conflicts with the global CPack.cmake (On MacOS X
# file names are case-insensitive)
#
include (cmake/package.cmake)
#
# RPM build environment
# CPACK is only used for .tar.gz package generation.
# To build an RPM we need a source package,
# so rpm.cmake depends on package.cmake.
#
include (cmake/rpm.cmake)

add_subdirectory(src)
add_subdirectory(extra)
add_subdirectory(test)
add_subdirectory(doc)

install (FILES README.md LICENSE
         DESTINATION share/doc/tarantool)

#
# tarantool info summary (used in server version output)
#
set(TARANTOOL_OPTIONS "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_TRACE=${ENABLE_TRACE} -DENABLE_BACKTRACE=${ENABLE_BACKTRACE}")
set(TARANTOOL_BUILD "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_BUILD_TYPE}")
set(TARANTOOL_C_COMPILER ${CMAKE_C_COMPILER})
set(TARANTOOL_CXX_COMPILER ${CMAKE_CXX_COMPILER})

#
# Output compile-time defines into config.h. Do it at the end
# of the script to make sure all variables are set.
#
configure_file(
    "${PROJECT_SOURCE_DIR}/src/trivia/config.h.cmake"
    "${PROJECT_BINARY_DIR}/src/trivia/config.h"
    )
message (STATUS "")
message (STATUS "Tarantool configuration is complete:")
message (STATUS "")
message (STATUS "VERSION: ${PACKAGE_VERSION}")
message (STATUS "BUILD: ${TARANTOOL_BUILD}")
message (STATUS "C_COMPILER: ${TARANTOOL_C_COMPILER}")
message (STATUS "CXX_COMPILER: ${TARANTOOL_CXX_COMPILER}")
message (STATUS "C_FLAGS:${TARANTOOL_C_FLAGS}")
message (STATUS "CXX_FLAGS:${TARANTOOL_CXX_FLAGS}")
message (STATUS "PREFIX: ${CMAKE_INSTALL_PREFIX}")
message (STATUS "MODULE_DIR: ${MODULE_DIR}")
message (STATUS "ENABLE_SSE2: ${ENABLE_SSE2}")
message (STATUS "ENABLE_AVX: ${ENABLE_AVX}")
message (STATUS "ENABLE_GCOV: ${ENABLE_GCOV}")
message (STATUS "ENABLE_GPROF: ${ENABLE_GPROF}")
message (STATUS "ENABLE_VALGRIND: ${ENABLE_VALGRIND}")
message (STATUS "ENABLE_TRACE: ${ENABLE_TRACE}")
message (STATUS "ENABLE_BACKTRACE: ${ENABLE_BACKTRACE} (symbol resolve: ${HAVE_BFD})")
message (STATUS "ENABLE_BUNDLED_LUAJIT: ${ENABLE_BUNDLED_LUAJIT}")
message (STATUS "ENABLE_BUNDLED_LIBEV: ${ENABLE_BUNDLED_LIBEV}")
message (STATUS "ENABLE_BUNDLED_LIBEIO: ${ENABLE_BUNDLED_LIBEIO}")
message (STATUS "ENABLE_BUNDLED_LIBCORO: ${ENABLE_BUNDLED_LIBCORO}")
message (STATUS "ENABLE_DOC: ${ENABLE_DOC}")
list_optional_packages()
if (TARGET_OS_DARWIN)
    message (STATUS "DARWIN_BUILD_TYPE: ${DARWIN_BUILD_TYPE}")
endif()
message (STATUS "")
message (STATUS "To view or modify configuration results, check out CMakeCache.txt.")
message (STATUS "")
