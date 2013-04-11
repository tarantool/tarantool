cmake_minimum_required(VERSION 2.6)

project(tarantool)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_INCLUDE_PATH})

include(CheckLibraryExists)
include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(CheckSymbolExists)
include(CheckCSourceRuns)
include(CheckCXXSourceRuns)
include(TestBigEndian)
include(CheckFunctionExists)
include(CheckBuiltInFunctionExists)

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
set (TARANTOOL_CLIENTS "tarantool_checksum")
# Define PACKAGE macro in config.h
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

set(CMAKE_OBJC_FLAGS)
set(CMAKE_OBJCXX_FLAGS)

include(cmake/utils.cmake)

# the order is significant: we need to know os and compiler to configure libs
include(cmake/arch.cmake)
include(cmake/os.cmake)
include(cmake/compiler.cmake)
include(cmake/simd.cmake)
include(cmake/profile.cmake)

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

set(HAVE_NON_C99_PTHREAD_H 0)
if (CMAKE_COMPILER_IS_GNUCC)

    set(CMAKE_REQUIRED_FLAGS "-std=gnu99 -Wall -fexceptions -pthread")
    check_symbol_exists(__pthread_cleanup_routine pthread.h test1)

    set(HAVE_NON_C99_PTHREAD_H OFF)
    if (test1)
        try_compile(HAVE_NON_C99_PTHREAD_H ${CMAKE_BINARY_DIR} 
            ${CMAKE_SOURCE_DIR}/cmake/pthread-broken-inline-test.c
            COMPILE_DEFINITIONS "-std=gnu99 -Wall -fexceptions")
    endif()
    unset(test1)

    if (HAVE_NON_C99_PTHREAD_H)
        message(WARNING "The system pthread.h is not compatible with the C99 mode."
            "A special workaround will be used during compilation."
            "If it does not work for you, please update system glibc.")
    endif()
endif()

#
# Enable 'make TAGS' target.
#
add_custom_target(TAGS COMMAND ctags -R --langmap=ObjectiveC:.m -e -f TAGS
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

#
# Define PACKAGE_VERSION -- a string constant with tarantool version.
#
set (PACKAGE_VERSION "")
execute_process (COMMAND ${GIT} describe HEAD
    OUTPUT_VARIABLE PACKAGE_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
#
# Make sure the version is set even if building
# out of source.
#
set (CPACK_PACKAGE_VERSION_MAJOR "1")
set (CPACK_PACKAGE_VERSION_MINOR "4")
set (CPACK_PACKAGE_VERSION_PATCH "9")
if (PACKAGE_VERSION STREQUAL "")
    set (PACKAGE_VERSION
        "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
endif()

#
# Specify where to look for include files.
#
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_BINARY_DIR}/include")
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
    set(CMAKE_REQUIRED_DEFINITIONS -DPACKAGE=${PACKAGE} -DPACKAGE_VERSION=${PACKAGE_VERSION})
    check_include_file(bfd.h HAVE_BFD_H)
    set(CMAKE_REQUIRED_DEFINITIONS)
    if (HAVE_BFD_LIB AND HAVE_BFD_H)
        set (HAVE_BFD True)
    endif()
endif()

option(ENABLE_STATIC "Perform static linking whenever possible." OFF)
if (ENABLE_STATIC)
    add_compile_flags("C;CXX" "-static")
endif()

#
# On CentOS the default gcc is way older than gcc 4.6, required
# to compile Tarantool. To make the package compiled with gcc 4.6
# usable on a system which has only gcc 4.1, statically link with
# builtin libgcc by default.
#
if (CMAKE_COMPILER_IS_GNUCC AND NOT CMAKE_COMPILER_IS_CLANG)
    add_compile_flags("C;CXX;OBJC;OBJCXX" "-static-libgcc")
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
# LibOBJC
#
set (ENABLE_BUNDLED_LIBOBJC_DEFAULT ON)
if (TARGET_OS_DARWIN)
    set (ENABLE_BUNDLED_LIBOBJC_DEFAULT OFF)
endif()
option(ENABLE_BUNDLED_LIBOBJC "Enable building of the bundled libobjc" ${ENABLE_BUNDLED_LIBOBJC_DEFAULT})

if (ENABLE_BUNDLED_LIBOBJC)
    include(BuildLibOBJC)
    libobjc_build()
    add_dependencies(build_bundled_libs objc)
    set(HAVE_LIBOBJC_BUNDLED ON)
else()
    if (CMAKE_COMPILER_IS_GNUCC)
        # Use compiler's objc library
        add_compile_flags("OBJC;OBJCXX" "-objc")
        set(LIBOBJC_LIBRARIES objc)
        message(STATUS "Use gcc Objective-C runtime")
    else()
        set(LIBOBJC_FIND_REQUIRED ON)
        find_package(LibOBJC)
    endif()
endif()


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
# Third-Party misc
#

include(BuildMisc)
libmisc_build()
add_dependencies(build_bundled_libs misc)


add_subdirectory(cfg)
add_subdirectory(connector)
add_subdirectory(src)
add_subdirectory(extra)
add_subdirectory(client)
add_subdirectory(test)
add_subdirectory(doc)

install (FILES README.md LICENSE doc/box-protocol.txt
         DESTINATION share/doc/tarantool)

include (cmake/package.cmake)

# tarantool info summary (used in server version output)
#
set(TARANTOOL_OPTIONS "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_STATIC=${ENABLE_STATIC}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_TRACE=${ENABLE_TRACE} -DENABLE_BACKTRACE=${ENABLE_BACKTRACE}")
set(TARANTOOL_OPTIONS "${TARANTOOL_OPTIONS} -DENABLE_CLIENT=${ENABLE_CLIENT}")
set(TARANTOOL_BUILD "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_BUILD_TYPE}")
set(TARANTOOL_C_COMPILER ${CMAKE_C_COMPILER})
set(TARANTOOL_CXX_COMPILER ${CMAKE_CXX_COMPILER})

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
message (STATUS "VERSION: ${PACKAGE_VERSION}")
message (STATUS "BUILD: ${TARANTOOL_BUILD}")
message (STATUS "C_COMPILER: ${TARANTOOL_C_COMPILER}")
message (STATUS "CXX_COMPILER: ${TARANTOOL_CXX_COMPILER}")
message (STATUS "C_FLAGS:${TARANTOOL_C_FLAGS}")
message (STATUS "CXX_FLAGS:${TARANTOOL_CXX_FLAGS}")
message (STATUS "OBJC_FLAGS:${TARANTOOL_OBJC_FLAGS}")
message (STATUS "OBJCXX_FLAGS:${TARANTOOL_OBJCXX_FLAGS}")
message (STATUS "PREFIX: ${CMAKE_INSTALL_PREFIX}")
message (STATUS "MODULES: ${TARANTOOL_MODULES}")
message (STATUS "ENABLE_STATIC: ${ENABLE_STATIC}")
message (STATUS "ENABLE_SSE2: ${ENABLE_SSE2}")
message (STATUS "ENABLE_AVX: ${ENABLE_AVX}")
message (STATUS "ENABLE_GCOV: ${ENABLE_GCOV}")
message (STATUS "ENABLE_GPROF: ${ENABLE_GPROF}")
message (STATUS "ENABLE_TRACE: ${ENABLE_TRACE}")
message (STATUS "ENABLE_BACKTRACE: ${ENABLE_BACKTRACE} (symbol resolve: ${HAVE_BFD})")
message (STATUS "ENABLE_CLIENT: ${ENABLE_CLIENT}")
message (STATUS "ENABLE_BUNDLED_LUAJIT: ${ENABLE_BUNDLED_LUAJIT}")
message (STATUS "ENABLE_BUNDLED_LIBOBJC: ${ENABLE_BUNDLED_LIBOBJC}")
message (STATUS "ENABLE_BUNDLED_LIBEV: ${ENABLE_BUNDLED_LIBEV}")
message (STATUS "ENABLE_BUNDLED_LIBEIO: ${ENABLE_BUNDLED_LIBEIO}")
message (STATUS "ENABLE_BUNDLED_LIBCORO: ${ENABLE_BUNDLED_LIBCORO}")
message (STATUS "ENABLE_DOC: ${ENABLE_DOC}")
message (STATUS "")
message (STATUS "To view or modify configuration results, check out CMakeCache.txt.")
message (STATUS "")
