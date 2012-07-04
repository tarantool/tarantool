include(cmake/check_objective_c_compiler.cmake)

# We support building with Clang and gcc. First check 
# what we're using for build.
if (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set(CMAKE_COMPILER_IS_CLANG true)
    set(CMAKE_COMPILER_IS_GNUCC false)
endif()

# Check Gcc version:
# GCC older than 4.1 is not supported.
if (CMAKE_COMPILER_IS_GNUCC)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
			OUTPUT_VARIABLE GCC_VERSION)
	if (GCC_VERSION VERSION_GREATER 4.1 OR GCC_VERSION VERSION_EQUAL 4.1)
		message(STATUS "GCC Version >= 4.1 -- ${GCC_VERSION}")
	else()
	    message (FATAL_ERROR "GCC version should be >= 4.1 -- ${GCC_VERSION}")
	endif()
endif()


#
# Tarantool uses 'coro' (coroutines) library to implement
# cooperative multi-tasking. Since coro.h is included
# universally, define the underlying implementation switch
# in the top level CMakeLists.txt, to ensure a consistent
# header file layout across the entire project.
#
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "amd64")
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCORO_ASM")
else()
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCORO_SJLJ")
endif()

#
# Perform build type specific configuration.
#
if (CMAKE_COMPILER_IS_GNUCC)
    set (CC_DEBUG_OPT "-ggdb")
else()
    set (CC_DEBUG_OPT "-g")
endif()

set (CMAKE_C_FLAGS_DEBUG "${CC_DEBUG_OPT} -O0")
set (CMAKE_C_FLAGS_RELWITHDEBUGINFO "${CC_DEBUG_OPT} -O2")
#
# Set flags for all include files: those maintained by us and
# coming from third parties.
# We must set -fno-omit-frame-pointer here, since we rely
# on frame pointer when getting a backtrace, and it must
# be used consistently across all object files.
# The same reasoning applies to -fno-stack-protector switch.
# Since we began using luajit, which uses gcc stack unwind
# internally, we also need to make sure all code is compiled
# with unwind info.
#
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-stack-protector")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fexceptions")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -funwind-tables")
if (CMAKE_COMPILER_IS_GNUCC)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fgnu89-inline")
endif()
if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
# Remove VALGRIND code and assertions in *any* type of release build.
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG -DNVALGRIND")
endif()

#
# GCC started to warn for unused result starting from 4.2, and
# this is when it introduced -Wno-unused-result
# GCC can also be built on top of llvm runtime (on mac).
#
check_c_compiler_flag("-Wno-unused-result" CC_HAS_WNO_UNUSED_RESULT)
check_c_compiler_flag("-Wno-unused-value" CC_HAS_WNO_UNUSED_VALUE)
check_c_compiler_flag("-Wno-bitwise-op-parentheses"
CC_HAS_WNO_WNO_BITWISE_OP_PARENTHESES)

#
# Tarantool code is written in GNU C dialect.
# Additionally, compile it with more strict flags than the rest
# of the code.
#
set (core_cflags "${core_cflags} -Wno-sign-compare")
set (core_cflags "${core_cflags} -Wno-strict-aliasing")
set (core_cflags "${core_cflags} -std=gnu99")
if(CMAKE_COMPILER_IS_GNUCC)
    set (core_cflags "${core_cflags} -Wall -Wextra")
elseif(CMAKE_COMPILER_IS_CLANG AND CC_HAS_WNO_UNUSED_RESULT)
    set (core_cflags "${core_cflags} -Wno-unused-result")
endif()

# Only add -Werror if it's a debug build, done by developers.
# Community builds should not cause extra trouble.
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (core_cflags "${core_cflags} -Werror")
endif()

# Mac ports get installed into /opt/local, hence:
if (TARGET_OS_DARWIN)
    include_directories("/opt/local/include")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/local/lib")
endif()

# Require pthread globally if compiling with gcc
if (CMAKE_COMPILER_IS_GNUCC)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
endif()

# CMake believes that Objective C is a flavor of C++, not C,
# and uses g++ compiler for .m files. Since talking CMake out
# of this idea is difficult, and since gcc or g++ are only
# front-ends to the language-specific compiler specified in
# -x option, simply use CXX flags to build Objective C files.
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS}")
#
# To enable @try/@catch/@finaly syntax in Objective C code,
# gcc requires this flag.
#
if (CMAKE_COMPILER_IS_GNUCC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-exceptions")
endif()
