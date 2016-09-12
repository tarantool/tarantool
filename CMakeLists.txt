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
include(FindPackageMessage)

find_program(ECHO echo)
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
include(cmake/module.cmake)
include(cmake/thread.cmake)

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
check_symbol_exists(posix_fadvise fcntl.h HAVE_POSIX_FADVISE)
check_symbol_exists(mremap sys/mman.h HAVE_MREMAP)

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
set(CMAKE_REQUIRED_LIBRARIES "")
if (TARGET_OS_LINUX)
    set(CMAKE_REQUIRED_LIBRARIES rt)
endif ()
check_symbol_exists(clock_gettime time.h HAVE_CLOCK_GETTIME_DECL)
if (HAVE_CLOCK_GETTIME_DECL AND TARGET_OS_DARWIN)
    # ensure clock_gettime() is declared and actually available
    # in runtime (gh-1777)
    check_c_source_runs(
        "#include <time.h>\nint main() {struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return 0;}"
        HAVE_CLOCK_GETTIME)
else ()
    set(HAVE_CLOCK_GETTIME ${HAVE_CLOCK_GETTIME_DECL})
endif ()
set(CMAKE_REQUIRED_LIBRARIES "")

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
# Get version
#

set (PACKAGE_VERSION "")
set (TARANTOOL_VERSION "")

# Try to get version from VERSION file
set(VERSION_FILE_ORIG "${PROJECT_SOURCE_DIR}/VERSION")
set(VERSION_FILE "${PROJECT_BINARY_DIR}/VERSION")
if (EXISTS "${VERSION_FILE_ORIG}")
    file (STRINGS "${VERSION_FILE_ORIG}" TARANTOOL_VERSION)
elseif (EXISTS "${VERSION_FILE}")
    file (STRINGS "${VERSION_FILE}" TARANTOOL_VERSION)
endif()

# Get git version only if source directory has .git repository, this
# avoids git to search .git repository in parent
# directories.
#
if (EXISTS "${CMAKE_SOURCE_DIR}/.git" AND GIT)
    execute_process (COMMAND ${GIT} describe --long HEAD
        OUTPUT_VARIABLE TARANTOOL_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    if (NOT ("${TARANTOOL_GIT_VERSION}" STREQUAL "${TARANTOOL_VERSION}"))
        set(TARANTOOL_VERSION "${TARANTOOL_GIT_VERSION}")
        message(STATUS "Generating VERSION file")
        file(WRITE ${VERSION_FILE} "${TARANTOOL_VERSION}\n")

        message(STATUS "Updating submodules")
        execute_process(COMMAND ${GIT} submodule update --init --recursive)
    endif()
endif()

if (NOT TARANTOOL_VERSION)
    message (FATAL_ERROR "Unable to retrive version from git or ${VERSION_FILE} file.")
endif()

#
# Split full version (git describe --long) to get components
#
string(REPLACE "-" "." TARANTOOL_VERSION_LIST ${TARANTOOL_VERSION})
string(REPLACE "." ";" TARANTOOL_VERSION_LIST ${TARANTOOL_VERSION_LIST})
LIST(GET TARANTOOL_VERSION_LIST 0 CPACK_PACKAGE_VERSION_MAJOR)
LIST(GET TARANTOOL_VERSION_LIST 1 CPACK_PACKAGE_VERSION_MINOR)
LIST(GET TARANTOOL_VERSION_LIST 2 CPACK_PACKAGE_VERSION_PATCH)
LIST(GET TARANTOOL_VERSION_LIST 3 CPACK_PACKAGE_VERSION_COMMIT)

set(PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}")
set(PACKAGE_VERSION "${PACKAGE_VERSION}.${CPACK_PACKAGE_VERSION_MINOR}")
set(PACKAGE_VERSION "${PACKAGE_VERSION}.${CPACK_PACKAGE_VERSION_PATCH}")
set(PACKAGE_VERSION "${PACKAGE_VERSION}.${CPACK_PACKAGE_VERSION_COMMIT}")

find_package_message(TARANTOOL_VERSION
    "Tarantool version is ${TARANTOOL_VERSION} (${PACKAGE_VERSION})"
    "${PACKAGE_VERSION}")

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

set(MODULE_PRODUCT "tarantool")
set(MODULE_LIBDIR "${CMAKE_INSTALL_LIBDIR}/${MODULE_PRODUCT}")
set(MODULE_LUADIR "${CMAKE_INSTALL_DATADIR}/${MODULE_PRODUCT}")
set(MODULE_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/${MODULE_PRODUCT}")
set(MODULE_FULL_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}/${MODULE_PRODUCT}")
set(MODULE_FULL_LUADIR "${CMAKE_INSTALL_FULL_DATADIR}/${MODULE_PRODUCT}")
set(MODULE_FULL_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}/${MODULE_PRODUCT}")
set(MODULE_LIBSUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}")

install(DIRECTORY DESTINATION ${MODULE_LUADIR})
install(DIRECTORY DESTINATION ${MODULE_LIBDIR})

include(cmake/multilib.cmake)

set(_datadirs)
list(APPEND _datadirs "/usr/local/share") # LuaRocks
list(APPEND _datadirs "${CMAKE_INSTALL_FULL_DATADIR}") # Install prefix
if(NOT CMAKE_CROSSCOMPILING AND EXISTS "/etc/gentoo-release")
    # LuaRocks on Gentoo
    list(APPEND _datadirs "/usr/${MULTILIB}/lua/luarocks/share")
endif()
list(APPEND _datadirs "/usr/share") # System packages
list(REMOVE_DUPLICATES _datadirs)
set(MODULE_LUAPATH)
foreach(dir
        "${MODULE_PRODUCT}"
        "lua/5.1")
    foreach(prefix IN LISTS _datadirs)
        list(APPEND MODULE_LUAPATH "${prefix}/${dir}/?.lua")
        list(APPEND MODULE_LUAPATH "${prefix}/${dir}/?/init.lua")
    endforeach()
endforeach()

set(_libdirs)
list(APPEND _libdirs "/usr/local/${MULTILIB}") # LuaRocks
list(APPEND _libdirs "${CMAKE_INSTALL_FULL_LIBDIR}") # Install prefix
if(NOT CMAKE_CROSSCOMPILING AND EXISTS "/etc/debian_version")
    # gh-1085: LuaRocks on Debian uses lib instead of lib/x86_64-linux-gnu/
    list(APPEND _libdirs "/usr/local/lib") # LuaRocks on Debian
endif()
if(NOT CMAKE_CROSSCOMPILING AND EXISTS "/etc/gentoo-release")
    # LuaRocks on Gentoo
    list(APPEND _libdirs "/usr/${MULTILIB}/lua/luarocks/lib")
endif()
if(NOT CMAKE_CROSSCOMPILING AND EXISTS "/etc/alpine-release")
    # LuaRocks on Alpine
    list(APPEND _libdirs "/usr/local/lib")
endif()
list(APPEND _libdirs "/usr/${MULTILIB}") # System packages
list(REMOVE_DUPLICATES _libdirs)
set(MODULE_LIBPATH)
foreach(dir
        "${MODULE_PRODUCT}"
        "lua/5.1")
    foreach(prefix IN LISTS _libdirs)
        list(APPEND MODULE_LIBPATH "${prefix}/${dir}/?${MODULE_LIBSUFFIX}")
    endforeach()
endforeach()

find_package_message(MODULE_LUAPATH "Lua package.path: ${MODULE_LUAPATH}"
    "${MODULE_LUAPATH}")
find_package_message(MODULE_LIBPATH "Lua package.cpath: ${MODULE_LIBPATH}"
    "${MODULE_LIBPATH}")


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
# ReadLine
#

set(Readline_FIND_REQUIRED ON)
find_package(Readline)

#
# LuaJIT
#
# Patched.
#
set(ENABLE_BUNDLED_LUAJIT ON)
include(luajit)

#
# LibEV
#
# Patched.
#
set(ENABLE_BUNDLED_LIBEV ON)
include(BuildLibEV)
libev_build()
add_dependencies(build_bundled_libs ev)

#
# LibEIO
#
# Patched.
#
#option(ENABLE_BUNDLED_LIBEIO "Enable building of the bundled libeio" ON)
set(ENABLE_BUNDLED_LIBEIO ON)
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

#
# LibGOPT
#

include(BuildLibGOPT)
libgopt_build()
add_dependencies(build_bundled_libs gopt)

#
# MsgPuck
#

option(ENABLE_BUNDLED_MSGPUCK "Enable building of the bundled MsgPuck" ON)
if (ENABLE_BUNDLED_MSGPUCK)
    add_library(msgpuck STATIC src/lib/msgpuck/msgpuck.c)
    set(MSGPUCK_LIBRARIES msgpuck)
    set(MSGPUCK_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/msgpuck)
    find_package_message(MsgPuck
        "Using bundled MsgPuck"
       "${MSGPUCK_LIBRARIES}:${MSGPUCK_INCLUDE_DIRS}")
else()
    set(MsgPuck_FIND_REQUIRED ON)
    find_package(MsgPuck)
endif()

#
# LibYAML
#

option(ENABLE_BUNDLED_LIBYAML "Enable building of the bundled libyaml" ON)
if (ENABLE_BUNDLED_LIBYAML)
    include(BuildLibYAML)
    libyaml_build()
    add_dependencies(build_bundled_libs yaml)
else()
    set(LIBYAML_FIND_REQUIRED ON)
    find_package(LibYAML)
endif()

#
# zstd
#

# Debian: missing zstd_static.h in libzstd-dev
# Fedora: not found
# => always use bundled version
include(BuildZSTD)
zstd_build()

#
# OpenSSL
#

find_package(OpenSSL)
if (OPENSSL_FOUND)
    message(STATUS "OpenSSL ${OPENSSL_VERSION} found")
    include_directories(${OPENSSL_INCLUDE_DIR})
else()
    message(FATAL_ERROR "Could NOT find OpenSSL development files (libssl-dev/openssl-devel package)")
endif()

#
# Third-Party misc
#

include(BuildMisc)
libmisc_build()
add_dependencies(build_bundled_libs misc)

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

if(NOT "${PROJECT_BINARY_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}")
    add_custom_target(distclean)
    add_custom_command(TARGET distclean
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${PROJECT_BINARY_DIR}"
        COMMENT "Removing the build directory and its content"
    )
elseif(IS_DIRECTORY .git AND GIT)
    add_custom_target(distclean)
    add_custom_command(TARGET distclean
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND ${GIT} submodule foreach --recursive git clean -f -X -d
        COMMAND ${GIT} clean -f -X -d
        COMMENT "Removing all build files from the source directory"
    )
endif()

#
# tarantool info summary (used in server version output)
#
set(TARANTOOL_OPTIONS "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_BACKTRACE=${ENABLE_BACKTRACE}")
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

set(PREFIX ${CMAKE_INSTALL_PREFIX})
set(options VERSION BUILD C_COMPILER CXX_COMPILER C_FLAGS CXX_FLAGS PREFIX
    ENABLE_SSE2 ENABLE_AVX
    ENABLE_GCOV ENABLE_GPROF ENABLE_VALGRIND ENABLE_ASAN
    ENABLE_BACKTRACE
    HAVE_BFD
    ENABLE_DOC
    ENABLE_DIST
    ENABLE_BUNDLED_LIBYAML
    ENABLE_BUNDLED_MSGPUCK)
foreach(option IN LISTS options)
    if (NOT DEFINED ${option})
        set(value "${TARANTOOL_${option}}")
    else ()
        set(value "${${option}}")
    endif ()
    find_package_message("${option}" "${option}: ${value}" "${value}")
endforeach(option)
list_optional_packages()
message (STATUS "")
