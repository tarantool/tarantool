cmake_minimum_required(VERSION 2.6)
project(tarantool)
include(CheckLibraryExists)
include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(TestBigEndian)
find_program(ECHO echo)
find_program(CAT cat)
find_program(GIT git)
find_program(RAGEL ragel)
find_program(CONFETTI confetti)

#
# This instructs the rest of the build system what product
# and what modules to produce.
#
set (TARANTOOL_PRODUCT "silverbox")
set (TARANTOOL_MODULES "silverbox" "feeder")

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
# Perform operating-system specific configuration.
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(TARGET_OS_LINUX 1)
#
# Enable GNU glibc extentions.
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
#
# On 32-bit systems, support files larger than 2GB
# (see man page for feature_test_macros).
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    set(TARGET_OS_FREEBSD 1)
else()
    message (FATAL_ERROR "Unsupported platform -- ${CMAKE_SYSTEM_NAME}")
endif()

test_big_endian(HAVE_BYTE_ORDER_BIG_ENDIAN)

#
# We do not perform host-to-network byte order translation,
# and simply assume the machine is little-endian.
# We also do not bother with trying to avoid unaligned
# word access. Refuse to compile on rare hardware such as
# Sparc or Itanium.
#
if (${HAVE_BYTE_ORDER_BIG_ENDIAN} OR
    ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "sparc" OR
    ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "ia64" OR
    ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^alpha")
    message (FATAL_ERROR "Unsupported architecture -- ${CMAKE_SYSTEM_PROCESSOR}, ")
    message (FATAL_ERROR "Tarantool currently only supports little-endian hardware")
    message (FATAL_ERROR "with unaligned word access.")
endif()

#
# Some versions of GNU libc define non-portable __libc_stack_end
# which we use to determine the end (or beginning, actually) of
# stack. Find whether or not it's present.
check_library_exists("" __libc_stack_end "" HAVE_LIBC_STACK_END)

#
# Tarantool uses 'coro' (coroutines) library # to implement
# cooperative multi-tasking. Since coro.h is included
# universally, define the underlying implementation switch
# in the top level CMakeLists.txt, to ensure a consistent
# header file layout across the entire project.
#
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCORO_ASM")
else()
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCORO_SJLJ")
  set_target_properties(coro PROPERTIES COMPILE_FLAGS "-DCORO_SJLJ")
endif()
#
# Perform build type specific configuration.
#
set (CMAKE_C_FLAGS_DEBUG "-ggdb -O0")
set (CMAKE_C_FLAGS_RELWITHDEBUGINFO "-ggdb -O2")
set (CMAKE_C_FLAGS_RELEASE "-DNDEBUG -DNVALGRIND")

#
# Enable 'make TAGS' target.
#
add_custom_target(TAGS COMMAND ctags -R -e -f TAGS
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
set (CPACK_PACKAGE_VERSION_MINOR "3")
set (CPACK_PACKAGE_VERSION_PATCH "5")
if (TARANTOOL_VERSION STREQUAL "")
    set (TARANTOOL_VERSION
        "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
endif()


#
# Set flags for all include files: those maintained by us and
# coming from third parties.
# We must set -fno-omit-frame-pointer here, since we rely
# on frame pointer when getting a backtrace, and it must
# be used consistently across all object files.
# The same reasoning applies to -fno-stack-protector switch.
#
set (CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fno-stack-protector")
#
# Tarantool code is written in GNU C dialect.
# Additionally, compile it with more strict flags than the rest
# of the code.
#
if (${CMAKE_BUILD_TYPE} STREQUAL "None")
    set (core_cflags "-std=gnu99")
else()
    set (core_cflags "-std=gnu99 -Wall -Wextra -Werror")
    set (core_cflags "${core_cflags} -Wno-sign-compare -Wno-strict-aliasing")
endif()

#
# Specify where to look for include files.
#
include_directories("${PROJECT_SOURCE_DIR}")
include_directories("${PROJECT_SOURCE_DIR}/include")
include_directories("${PROJECT_BINARY_DIR}/include")

#
# Now handle all configuration options.
#
option(ENABLE_GCOV "Enable integration with gcov, a code coverage program" ON)
if (ENABLE_GCOV)
    check_library_exists (gcov __gcov_flush  ""  HAVE_GCOV)
    if (NOT HAVE_GCOV)
        message (FATAL_ERROR
                 "ENABLE_GCOV option requested but gcov library is not found")
    endif()
endif()

option(ENABLE_TRACE "Enable debug trace of tarantool_silverbox execution to
a file specified in TARANTOOL_TRACE environment variable" ON)

option(ENABLE_BACKTRACE "Enable output of fiber backtrace information in 'show
fiber' administrative command. Only works on x86 architectures, if compiled
with gcc. If GNU binutils and binutils-dev libraries are installed, backtrace
is output with resolved function (symbol) names. Otherwise only frame
addresses are printed." ${CMAKE_COMPILER_IS_GNUCC})

set (HAVE_BFD False)
if (ENABLE_BACKTRACE)
    if (NOT ${CMAKE_COMPILER_IS_GNUCC} OR
        NOT (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86"))
        # We only know this option to work with gcc
        # on x86 architecture.
        message (FATAL_ERROR "ENABLE_BACKTRACE option is set but the system is not x86 based (${CMAKE_SYSTEM_PROCESSOR}) or the compiler is not GNU GCC (${CMAKE_C_COMPILER}).")
    endif()
# Use GNU bfd if present.
    check_library_exists (bfd bfd_init ""  HAVE_BFD_LIB)
    check_include_file(bfd.h HAVE_BFD_H)
    if (HAVE_BFD_LIB AND HAVE_BFD_H)
        set (HAVE_BFD True)
        message (STATUS "Found GNU bfd headers and libs, enabling symbol ")
        message (STATUS "resolve in backtraces.")
    elseif (NOT HAVE_BFD_LIB)
        message (STATUS "Not found GNU bfd binaries, no symbol resolve.")
    elseif (NOT HAVE_BFD_H)
        message (STATUS "Not found GNU bfd headers, no symbol resolve. ")
        message (STATUS "Consider installing binutils-dev.")
    endif()
endif()

add_subdirectory(third_party)
add_subdirectory(cfg)
add_subdirectory(connector)
add_subdirectory(core)
add_subdirectory(mod)
add_subdirectory(test)

install (FILES README LICENSE doc/silverbox-protocol.txt
         DESTINATION doc)

include (cmake/tarantool_cpack.cmake)
#
# Output compile-time defines into config.h. Do it at the end
# of the script to make sure all variables are set.
#
configure_file(
    "${PROJECT_SOURCE_DIR}/include/config.h.cmake"
    "${PROJECT_BINARY_DIR}/include/config.h"
    )

message (STATUS "")
message (STATUS "Successfully configured Tarantool infrastructure on ")
message (STATUS "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}, build type '${CMAKE_BUILD_TYPE}'. ")
message (STATUS "The product you're building is \"${TARANTOOL_PRODUCT}\", enabled modules are ")
message (STATUS "${TARANTOOL_MODULES}.")
message (STATUS "Please check out CMakeCache.txt to view or modify configuration results.")
message (STATUS "")
message (STATUS "*** The following options are on in this configuration: ***")
message (STATUS "ENABLE_GCOV: ${ENABLE_GCOV}")
message (STATUS "ENABLE_TRACE: ${ENABLE_TRACE}")
message (STATUS "ENABLE_BACKTRACE: ${ENABLE_BACKTRACE}")
message (STATUS "Backtrace is with symbol resolve: ${HAVE_BFD}")
message (STATUS "")
