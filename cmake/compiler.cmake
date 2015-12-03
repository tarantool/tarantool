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

#
# Hard coding the compiler version is ugly from cmake POV, but 
# at least gives user a friendly error message. The most critical
# demand for C++ compiler is support of C++11 lambdas, added
# only in version 4.5 https://gcc.gnu.org/projects/cxx0x.html
#
if (CMAKE_COMPILER_IS_GNUCC)
# cmake 2.8.9 and earlier doesn't support CMAKE_CXX_COMPILER_VERSION
       if (NOT CMAKE_CXX_COMPILER_VERSION)
               execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
                               OUTPUT_VARIABLE CMAKE_CXX_COMPILER_VERSION)
       endif()
       if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.5)
               message(FATAL_ERROR "
               Your GCC version is ${CMAKE_CXX_COMPILER_VERSION}, please update
                       ")
       endif()
endif()

#
# Check supported standards
#
if((NOT HAVE_STD_C11 AND NOT HAVE_STD_GNU99) OR
   (NOT HAVE_STD_CXX11 AND NOT HAVE_STD_GNUXX0X))
    set(CMAKE_REQUIRED_FLAGS "-std=c11")
    check_c_source_compiles("
    /*
     * FreeBSD 10 ctype.h header fail to compile on gcc4.8 in c11 mode.
     * Make sure we aren't affected.
     */
    #include <ctype.h>
    int main(void) { return 0; }
    " HAVE_STD_C11)
    set(CMAKE_REQUIRED_FLAGS "-std=gnu99")
    check_c_source_compiles("int main(void) { return 0; }" HAVE_STD_GNU99)
    set(CMAKE_REQUIRED_FLAGS "-std=c++11")
    check_cxx_source_compiles("int main(void) { return 0; }" HAVE_STD_CXX11)
    set(CMAKE_REQUIRED_FLAGS "-std=gnu++0x")
    check_cxx_source_compiles("int main(void) { return 0; }" HAVE_STD_GNUXX0X)
    set(CMAKE_REQUIRED_FLAGS "")
endif()
if((NOT HAVE_STD_C11 AND NOT HAVE_STD_GNU99) OR
   (NOT HAVE_STD_CXX11 AND NOT HAVE_STD_GNUXX0X))
    message (FATAL_ERROR
        "${CMAKE_C_COMPILER} should support -std=c11 or -std=gnu99. "
        "${CMAKE_CXX_COMPILER} should support -std=c++11 or -std=gnu++0x. "
        "Please consider upgrade to gcc 4.5+ or clang 3.2+.")
endif()

#
# Check for an omp support
#
set(CMAKE_REQUIRED_FLAGS "-fopenmp -Werror")
check_cxx_source_compiles("int main(void) {
#pragma omp parallel
    {
    }
    return 0;
}" HAVE_OPENMP)
set(CMAKE_REQUIRED_FLAGS "")

if (NOT HAVE_OPENMP)
    add_compile_flags("C;CXX" -Wno-unknown-pragmas)
endif()

#
# Perform build type specific configuration.
#
check_c_compiler_flag("-ggdb" CC_HAS_GGDB)
if (CC_HAS_GGDB)
    set (CC_DEBUG_OPT "-ggdb")
endif()

set (CMAKE_C_FLAGS_DEBUG
    "${CMAKE_C_FLAGS_DEBUG} ${CC_DEBUG_OPT} -O0")
set (CMAKE_C_FLAGS_RELWITHDEBINFO
    "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${CC_DEBUG_OPT} -O2")
set (CMAKE_CXX_FLAGS_DEBUG
    "${CMAKE_CXX_FLAGS_DEBUG} ${CC_DEBUG_OPT} -O0")
set (CMAKE_CXX_FLAGS_RELWITHDEBINFO
    "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CC_DEBUG_OPT} -O2")

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
# In C a global variable without a storage specifier (static/extern) and
# without an initialiser is called a ’tentative definition’. The
# language permits multiple tentative definitions in the single
# translation unit; i.e. int foo; int foo; is perfectly ok. GNU
# toolchain goes even further, allowing multiple tentative definitions
# in *different* translation units. Internally, variables introduced via
# tentative definitions are implemented as ‘common’ symbols. Linker
# permits multiple definitions if they are common symbols, and it picks
# one arbitrarily for inclusion in the binary being linked.
#
# -fno-common forces GNU toolchain to behave in a more
# standard-conformant way in respect to tentative definitions and it
# prevents common symbols generation. Since we are a cross-platform
# project it really makes sense. There are toolchains that don’t
# implement GNU style handling of the tentative definitions and there
# are platforms lacking proper support for common symbols (osx).
#

add_compile_flags("C;CXX"
    "-fno-common"
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
    if (HAVE_STD_C11)
        add_compile_flags("C" "-std=c11")
    else()
        add_compile_flags("C" "-std=gnu99")
    endif()

    if (HAVE_STD_CXX11)
        add_compile_flags("CXX" "-std=c++11")
    else()
        add_compile_flags("CXX" "-std=gnu++0x")
    endif()

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

    if (CMAKE_COMPILER_IS_GNUCC)
        # A workaround for Redhat Developer Toolset 2.x on RHEL/CentOS 5.x
        add_compile_flags("C" "-fno-gnu89-inline")
    endif()

    add_definitions("-D__STDC_FORMAT_MACROS=1")
    add_definitions("-D__STDC_LIMIT_MACROS=1")
    add_definitions("-D__STDC_CONSTANT_MACROS=1")

    # Only add -Werror if it's a debug build, done by developers.
    # Release builds should not cause extra trouble.
    if ((${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        AND HAVE_STD_C11 AND HAVE_STD_CXX11)
        add_compile_flags("C;CXX" "-Werror")
    endif()
endmacro(enable_tnt_compile_flags)

if (HAVE_OPENMP)
    add_compile_flags("C;CXX" "-fopenmp")
endif()

#
# GCC started to warn for unused result starting from 4.2, and
# this is when it introduced -Wno-unused-result
# GCC can also be built on top of llvm runtime (on mac).
#

check_c_compiler_flag("-Wno-unused-const-variable" CC_HAS_WNO_UNUSED_CONST_VARIABLE)
check_c_compiler_flag("-Wno-unused-result" CC_HAS_WNO_UNUSED_RESULT)
check_c_compiler_flag("-Wno-unused-value" CC_HAS_WNO_UNUSED_VALUE)
check_c_compiler_flag("-fno-strict-aliasing" CC_HAS_FNO_STRICT_ALIASING)
check_c_compiler_flag("-Wno-comment" CC_HAS_WNO_COMMENT)
check_c_compiler_flag("-Wno-parentheses" CC_HAS_WNO_PARENTHESES)
check_c_compiler_flag("-Wno-undefined-inline" CC_HAS_WNO_UNDEFINED_INLINE)

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
