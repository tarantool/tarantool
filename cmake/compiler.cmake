#
# Check if the same compile family is used for both C and CXX
#
if (NOT (CMAKE_C_COMPILER_ID STREQUAL CMAKE_CXX_COMPILER_ID))
    message(WARNING "CMAKE_C_COMPILER_ID (${CMAKE_C_COMPILER_ID}) is different "
                    "from CMAKE_CXX_COMPILER_ID (${CMAKE_CXX_COMPILER_ID})."
                    "The final binary may be unusable.")
endif()

# We support building with Clang and gcc. First check 
# what we're using for build.
#
if (CMAKE_C_COMPILER_ID STREQUAL Clang)
    set(CMAKE_COMPILER_IS_CLANG  ON)
    set(CMAKE_COMPILER_IS_GNUCC  OFF)
    set(CMAKE_COMPILER_IS_GNUCXX OFF)
endif()

if(CMAKE_COMPILER_IS_GNUCC)
    # gcc and g++ >= 4.5 are supported
    execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
        OUTPUT_VARIABLE CC_VERSION)
    if (CC_VERSION VERSION_LESS 4.5)
        message (FATAL_ERROR
            "${CMAKE_C_COMPILER} version should be >= 4.5 -- ${CC_VERSION}")
    endif()
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
        OUTPUT_VARIABLE CXX_VERSION)
    if (CXX_VERSION VERSION_LESS 4.5)
        message (FATAL_ERROR
            "${CMAKE_CXX_COMPILER} version should be >= 4.5 -- ${CXX_VERSION}")
    endif()
endif()


#
# Perform build type specific configuration.
#
if (CMAKE_COMPILER_IS_GNUCC)
    set (CC_DEBUG_OPT "-ggdb")
else()
    set (CC_DEBUG_OPT "-g")
endif()

set (CMAKE_C_FLAGS_DEBUG
    "${CMAKE_C_FLAGS_DEBUG} ${CC_DEBUG_OPT} -O0")
set (CMAKE_C_FLAGS_RELWITHDEBUGINFO
    "${CMAKE_C_FLAGS_RELWITHDEBUGINFO} ${CC_DEBUG_OPT} -O2")
set (CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} ${CC_DEBUG_OPT} -O0")
set (CMAKE_CXX_FLAGS_RELWITHDEBUGINFO
    "${CMAKE_CXX_FLAGS_RELWITHDEBUGINFO} ${CC_DEBUG_OPT} -O2")

unset(CC_DEBUG_OPT)

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

add_compile_flags("C;CXX"
    "-fno-omit-frame-pointer"
    "-fno-stack-protector"
    "-fexceptions"
    "-funwind-tables")

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
# Remove VALGRIND code and assertions in *any* type of release build.
    add_definitions("-DNDEBUG" "-DNVALGRIND")
endif()

macro(enable_tnt_compile_flags)
    # Tarantool code is written in GNU C dialect.
    # Additionally, compile it with more strict flags than the rest
    # of the code.

    # Set standard
    if (CMAKE_COMPILER_IS_CLANG OR CC_VERSION VERSION_GREATER 4.7 OR
        CC_VERSION VERSION_EQUAL 4.7)
        add_compile_flags("C" "-std=c11")
        add_compile_flags("CXX" "-std=c++11")
    else()
        add_compile_flags("C" "-std=gnu99")
        add_compile_flags("CXX" "-std=gnu++0x")
    endif()

    # Disable Run-time type information
    add_compile_flags("CXX" "-fno-rtti")

    add_compile_flags("C;CXX"
        "-Wall"
        "-Wextra"
        "-Wno-sign-compare"
        "-Wno-strict-aliasing"
    )

    if (CMAKE_COMPILER_IS_GNUCXX)
        # G++ bug. http://gcc.gnu.org/bugzilla/show_bug.cgi?id=31488
        add_compile_flags("CXX"
            "-Wno-invalid-offsetof"
        )
    endif()

    add_definitions("-D__STDC_FORMAT_MACROS=1")
    add_definitions("-D__STDC_LIMIT_MACROS=1")
    add_definitions("-D__STDC_CONSTANT_MACROS=1")

    # Only add -Werror if it's a debug build, done by developers using GCC.
    # Community builds should not cause extra trouble.
   if (${CMAKE_BUILD_TYPE} STREQUAL "Debug" AND CMAKE_COMPILER_IS_GNUCC)
       add_compile_flags("C;CXX" "-Werror")
   endif()
endmacro(enable_tnt_compile_flags)

#
# GCC started to warn for unused result starting from 4.2, and
# this is when it introduced -Wno-unused-result
# GCC can also be built on top of llvm runtime (on mac).
#

check_c_compiler_flag("-Wno-unused-result" CC_HAS_WNO_UNUSED_RESULT)
check_c_compiler_flag("-Wno-unused-value" CC_HAS_WNO_UNUSED_VALUE)
check_c_compiler_flag("-fno-strict-aliasing" CC_HAS_FNO_STRICT_ALIASING)
check_c_compiler_flag("-Wno-comment" CC_HAS_WNO_COMMENT)
check_c_compiler_flag("-Wno-parentheses" CC_HAS_WNO_PARENTHESES)

if (CMAKE_COMPILER_IS_CLANG OR CMAKE_COMPILER_IS_GNUCC)
    set(HAVE_BUILTIN_CTZ 1)
    set(HAVE_BUILTIN_CTZLL 1)
    set(HAVE_BUILTIN_CLZ 1)
    set(HAVE_BUILTIN_CLZLL 1)
    set(HAVE_BUILTIN_POPCOUNT 1)
    set(HAVE_BUILTIN_POPCOUNTLL 1)
    set(HAVE_BUILTIN_BSWAP32 1)
    set(HAVE_BUILTIN_BSWAP64 1)
else()
    set(HAVE_BUILTIN_CTZ 0)
    set(HAVE_BUILTIN_CTZLL 0)
    set(HAVE_BUILTIN_CLZ 0)
    set(HAVE_BUILTIN_CLZLL 0)
    set(HAVE_BUILTIN_POPCOUNT 0)
    set(HAVE_BUILTIN_POPCOUNTLL 0)
    set(HAVE_BUILTIN_BSWAP32 0)
    set(HAVE_BUILTIN_BSWAP64 0)
endif()

if (NOT HAVE_BUILTIN_CTZ OR NOT HAVE_BUILTIN_CTZLL)
    # Check if -D_GNU_SOURCE has been defined and add this flag to
    # CMAKE_REQUIRED_DEFINITIONS in order to get check_prototype_definition work
    get_property(var DIRECTORY PROPERTY COMPILE_DEFINITIONS)
    list(FIND var "_GNU_SOURCE" var)
    if (NOT var EQUAL -1)
        set(CMAKE_REQUIRED_FLAGS "-Wno-error")
        set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
        check_c_source_compiles("#include <string.h>\n#include <strings.h>\nint main(void) { return ffsl(0L); }"
            HAVE_FFSL)
        check_c_source_compiles("#include <string.h>\n#include <strings.h>\nint main(void) { return ffsll(0UL); }"
            HAVE_FFSLL)
    endif()
endif()
