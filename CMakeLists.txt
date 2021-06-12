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
include(ExternalProject)

find_program(ECHO echo)
find_program(CAT cat)
find_program(BASH bash)
find_program(GIT git)
find_program(LD ld)
find_program(CTAGS ctags)
find_program(LUACHECK luacheck ENV PATH)

# Define PACKAGE macro in tarantool/config.h
set(PACKAGE "Tarantool" CACHE STRING "Package name.")

#
# Set default build type to Debug. This is to ease a developer's
# life. Release binaries are built by BuildBot automatically anyway.
#
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

#
# Check submodules
#
function(update_submodules)
    message(STATUS "Updating submodules")
    execute_process(COMMAND ${GIT} submodule update --init --recursive
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endfunction()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/src/lib/small/CMakeLists.txt)
    if (EXISTS "${CMAKE_SOURCE_DIR}/.git" AND GIT)
        update_submodules()
    else()
        message(FATAL_ERROR "Failed to find submodules")
    endif()
endif()

# Define GNU standard installation directories
include(GNUInstallDirs)

include(cmake/utils.cmake)
include(cmake/pod2man.cmake)
# the order is significant: we need to know os and compiler to configure libs
include(cmake/arch.cmake)
include(cmake/os.cmake)
include(cmake/compiler.cmake)
# NO_POLICY_SCOPE is to suppress CMP0069 warnings on the unset
# policy.
include(cmake/lto.cmake NO_POLICY_SCOPE)
include(cmake/simd.cmake)
include(cmake/atomic.cmake)
include(cmake/profile.cmake)
include(cmake/module.cmake)
include(cmake/thread.cmake)

set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")

check_symbol_exists(MAP_ANON sys/mman.h HAVE_MAP_ANON)
check_symbol_exists(MAP_ANONYMOUS sys/mman.h HAVE_MAP_ANONYMOUS)
check_symbol_exists(MADV_DONTNEED sys/mman.h HAVE_MADV_DONTNEED)
check_include_file(sys/time.h HAVE_SYS_TIME_H)
check_include_file(cpuid.h HAVE_CPUID_H)
check_include_file(sys/prctl.h HAVE_PRCTL_H)

check_symbol_exists(O_DSYNC fcntl.h HAVE_O_DSYNC)
check_symbol_exists(fdatasync unistd.h HAVE_FDATASYNC)
check_symbol_exists(pthread_yield pthread.h HAVE_PTHREAD_YIELD)
check_symbol_exists(sched_yield sched.h HAVE_SCHED_YIELD)
check_symbol_exists(posix_fadvise fcntl.h HAVE_POSIX_FADVISE)
check_symbol_exists(fallocate fcntl.h HAVE_FALLOCATE)
check_symbol_exists(mremap sys/mman.h HAVE_MREMAP)

check_function_exists(sync_file_range HAVE_SYNC_FILE_RANGE)
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
# According to `man 2 clockgettime` the glibc wrapper requires
# -lrt on glibc versions before 2.17. We need to check whether
# the function is available without -lrt to set this linker option
# conditionally.
check_function_exists(clock_gettime HAVE_CLOCK_GETTIME_WITHOUT_RT)

check_symbol_exists(__get_cpuid cpuid.h HAVE_CPUID)
check_symbol_exists(strlcpy string.h HAVE_STRLCPY)

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
list(APPEND tagsExclude "--exclude=.git/*")
list(APPEND tagsExclude "--exclude=.pc/*")
list(APPEND tagsExclude "--exclude=patches/*")
add_custom_target(tags COMMAND ${CTAGS} -R ${tagsExclude} -f tags
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_custom_target(ctags DEPENDS tags)

#
# Enable 'make luacheck' target.
#
# The code looks tricky, because of luacheck side problems
# (see [1]).
#
# The following circumstances may lead to missing of source files
# or exclusions:
#
# * Calling `luacheck "${dir}/.luacheckrc" "${dir}"` from
#   outside of ${dir} or when ${dir} is not a real path.
# * Using of a path with symlink components in --exclude-files.
#
# We should exclude the build directory (when it is inside the
# source tree), at least because it contains LuaJIT's generated
# files and the temporary directory for testing (test/var).
#
# .luacheckrc provides corresponding exclusion rules for the
# in-source build case (`cmake .`).
#
# [1]: https://github.com/mpeterv/luacheck/issues/208
#
set(EXCLUDE_FILES)
get_filename_component(BINARY_REALPATH "${PROJECT_BINARY_DIR}" REALPATH)
get_filename_component(SOURCE_REALPATH "${PROJECT_SOURCE_DIR}" REALPATH)
file_is_in_directory(BINARY_DIR_INSIDE_SOURCE_DIR "${BINARY_REALPATH}"
                     "${SOURCE_REALPATH}")
if (BINARY_DIR_INSIDE_SOURCE_DIR)
    set(EXCLUDE_FILES --exclude-files "${BINARY_REALPATH}/**/*.lua")
endif()
add_custom_target(luacheck DEPENDS LuaJIT-luacheck)
add_custom_command(TARGET luacheck
    COMMAND ${LUACHECK} --codes --config .luacheckrc . ${EXCLUDE_FILES}
    WORKING_DIRECTORY ${SOURCE_REALPATH}
    COMMENT "Perform static analysis of Lua code"
)
unset(BINARY_REALPATH)
unset(SOURCE_REALPATH)
unset(BINARY_DIR_INSIDE_SOURCE_DIR)
unset(EXCLUDE_FILES)

if (WITH_JEPSEN)
    ExternalProject_Add(
        jepsen-tests
        GIT_REPOSITORY https://github.com/tarantool/jepsen.tarantool
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        TEST_COMMAND ""
    )

    #
    # Enable 'make run-jepsen' target.
    #

    add_custom_target(run-jepsen DEPENDS jepsen-tests)
    add_custom_command(TARGET run-jepsen
        COMMAND ${BASH} ${PROJECT_SOURCE_DIR}/tools/run-jepsen-tests.sh
				${PROJECT_SOURCE_DIR}
                                ${PROJECT_BINARY_DIR}
        COMMENT "Running Jepsen tests"
    )
endif()

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

        update_submodules()
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
include_directories(${PROJECT_SOURCE_DIR}/src/lib/small/include)
include_directories(${PROJECT_SOURCE_DIR}/src/lib/small/third_party)
include_directories(${PROJECT_SOURCE_DIR}/src/lib/core)
include_directories(${PROJECT_SOURCE_DIR}/third_party)

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

# Debian: missing zstd_static.h in libzstd-dev
# Fedora: not found
# => use bundled version by default

option(ENABLE_BUNDLED_ZSTD "Enable building of the bundled zstd" ON)
if (ENABLE_BUNDLED_ZSTD)
    include(BuildZSTD)
    zstd_build()
    add_dependencies(build_bundled_libs zstd)
else()
    set(ZSTD_FIND_REQUIRED ON)
    find_package(ZSTD)
endif()

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
# OpenSSL can require Z library (depending on build time options), so we add
# it to libraries list in case of static openssl linking.
#
if(OPENSSL_USE_STATIC_LIBS)
    if(BUILD_STATIC)
        find_library(Z_LIBRARY libz.a)
    else()
        find_library(Z_LIBRARY z)
    endif()
    set(OPENSSL_LIBRARIES ${OPENSSL_LIBRARIES} ${Z_LIBRARY})
endif()

#
# Curl
#
option(ENABLE_BUNDLED_LIBCURL "Enable building of the bundled libcurl" ON)
option(BUNDLED_LIBCURL_USE_ARES "Build curl with bundled c-ares"
       ${ENABLE_BUNDLED_LIBCURL})
if (ENABLE_BUNDLED_LIBCURL)
    if(BUNDLED_LIBCURL_USE_ARES)
        include(BuildAres)
        ares_build()
    endif()
    include(BuildLibCURL)
    curl_build()
    add_dependencies(build_bundled_libs bundled-libcurl)
else()
    set(CURL_FIND_REQUIRED ON)
    find_package(CURL)
endif()

#
# XXhash
#
include(BuildLibXXhash)
libxxhash_build()
add_dependencies(build_bundled_libs xxhash)

# Install headers.
if (ENABLE_BUNDLED_LIBCURL)
    install(DIRECTORY "${CURL_INCLUDE_DIRS}/curl"
            DESTINATION ${MODULE_FULL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.h")
endif()

#
# Export libcurl symbols if the library is linked statically.
#
if (ENABLE_BUNDLED_LIBCURL OR BUILD_STATIC)
    set(EXPORT_LIBCURL_SYMBOLS ON)
else()
    set(EXPORT_LIBCURL_SYMBOLS OFF)
endif()
message(STATUS "EXPORT_LIBCURL_SYMBOLS: ${EXPORT_LIBCURL_SYMBOLS}")

#
# ReadLine
#

set(Readline_FIND_REQUIRED ON)
find_package(Readline)

#
# ICONV
#
set(ICONV_FIND_REQUIRED ON)
find_package(ICONV)

#
# ICU
#

set(ICU_FIND_REQUIRED ON)
find_package(ICU)

#
# LuaJIT
#
# Patched.
#
set(ENABLE_BUNDLED_LUAJIT ON)
set(LUAJIT_ENABLE_GC64_DEFAULT OFF)
if (TARGET_OS_DARWIN)
    # LuaJIT is unusable on OS X without enabled GC64
    # See https://github.com/tarantool/tarantool/issues/2643
    set(LUAJIT_ENABLE_GC64_DEFAULT ON)
endif()
option(LUAJIT_ENABLE_GC64 "Use 64-bit GC objects by default."
       ${LUAJIT_ENABLE_GC64_DEFAULT})
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
# MsgPuck
#

option(ENABLE_BUNDLED_MSGPUCK "Enable building of the bundled MsgPuck" ON)
if (ENABLE_BUNDLED_MSGPUCK)
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
# decNumber
#

include(BuildDecNumber)
decnumber_build()

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

option(WITH_NOTIFY_SOCKET "Enable notifications on NOTIFY_SOCKET" ON)

if (WITH_SYSTEMD AND NOT WITH_NOTIFY_SOCKET)
    message(FATAL_ERROR "WITH_NOTIFY_SOCKET must be enabled when WITH_SYSTEMD enabled")
endif()

if (WITH_NOTIFY_SOCKET)
    check_c_source_compiles("
        #include <sys/types.h>
        #include <sys/socket.h>
        int main(){ return MSG_NOSIGNAL; }
    " HAVE_MSG_NOSIGNAL)
    check_c_source_compiles("
        #include <sys/types.h>
        #include <sys/socket.h>
        int main(){ return SO_NOSIGPIPE; }
    " HAVE_SO_NOSIGPIPE)
    # Linux supports MSG_NOSIGNAL flag for sendmsg.
    # macOS lacks it, but has SO_NOSIGPIPE for setsockopt
    # to achieve same behavior.
    if (NOT HAVE_MSG_NOSIGNAL AND NOT HAVE_SO_NOSIGPIPE)
        message(FATAL_ERROR
            "No way to block SIGPIPE in sendmsg. Can not compile with WITH_NOTIFY_SOCKET"
        )
    endif()
endif()

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
set(options PACKAGE VERSION BUILD C_COMPILER CXX_COMPILER C_FLAGS CXX_FLAGS
    PREFIX
    ENABLE_SSE2 ENABLE_AVX
    ENABLE_GCOV ENABLE_GPROF ENABLE_VALGRIND ENABLE_ASAN ENABLE_UB_SANITIZER ENABLE_FUZZER
    ENABLE_BACKTRACE
    ENABLE_DOC
    ENABLE_DIST
    ENABLE_BUNDLED_LIBCURL
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
