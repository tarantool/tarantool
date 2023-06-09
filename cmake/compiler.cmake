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
# GCC started to warn for unused result starting from 4.2, and
# this is when it introduced -Wno-unused-result
# GCC can also be built on top of llvm runtime (on mac).
#

check_c_compiler_flag("-Wno-parentheses" CC_HAS_WNO_PARENTHESES)
check_c_compiler_flag("-Wno-parentheses-equality" CC_HAS_WNO_PARENTHESES_EQUALITY)
check_c_compiler_flag("-Wno-tautological-compare" CC_HAS_WNO_TAUTOLOGICAL_COMPARE)
check_c_compiler_flag("-Wno-misleading-indentation" CC_HAS_WNO_MISLEADING_INDENTATION)
check_c_compiler_flag("-Wno-varargs" CC_HAS_WNO_VARARGS)
check_c_compiler_flag("-Wno-implicit-fallthrough" CC_HAS_WNO_IMPLICIT_FALLTHROUGH)
check_c_compiler_flag("-Wno-cast-function-type" CC_HAS_WNO_CAST_FUNCTION_TYPE)

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

# Set flags for all include files: those maintained by us and
# coming from third parties.
# Since we began using luajit, which uses gcc stack unwind
# internally, we also need to make sure all code is compiled
# with unwind info.
add_compile_flags("C;CXX" "-fexceptions" "-funwind-tables")

# Enable emission of the DWARF CFI (Call Frame Information) directives to the
# assembler. This option is enabled by default on most compilers, but on GCC 7
# for AArch64 and older it wasn't, so turn it on explicitly. When enabled, the
# compiler emits .cfi_* directives that are required for the stack unwinding,
# and defines __GCC_HAVE_DWARF2_CFI_ASM, which is checked below.
add_compile_flags("C;CXX" "-fasynchronous-unwind-tables")

check_c_source_compiles(
    "
    #if defined(__x86_64__) && !__has_attribute(force_align_arg_pointer)
    #error
    #endif /* defined(__x86_64__) &&
            !__has_attribute(force_align_arg_pointer) */

    int main(){}
    " HAVE_FORCE_ALIGN_ARG_POINTER_ATTR)
check_c_source_compiles(
    "
    #if !defined(__clang__) /* clang supports CFI assembly by default */ && \
        (!defined(__GNUC__) || !defined(__GCC_HAVE_DWARF2_CFI_ASM))
    #error
    #endif  /* !defined(__clang__) && (!defined(__GNUC__) ||
               !defined(__GCC_HAVE_DWARF2_CFI_ASM)) */

    int main(){}
    " HAVE_CFI_ASM)
set(ENABLE_BACKTRACE_DEFAULT OFF)
# Disabled backtraces support on FreeBSD by default, because of
# gh-4278.
if(NOT TARGET_OS_FREEBSD AND HAVE_FORCE_ALIGN_ARG_POINTER_ATTR
    AND HAVE_CFI_ASM)
    set(ENABLE_BACKTRACE_DEFAULT ON)
endif()

option(ENABLE_BACKTRACE "Enable output of fiber backtrace information in \
                         'fiber.info()' command."
       ${ENABLE_BACKTRACE_DEFAULT})

check_include_file(stdatomic.h HAVE_STDATOMIC_H)
set(ENABLE_BUNDLED_LIBUNWIND_DEFAULT ON)
if(TARGET_OS_DARWIN OR NOT HAVE_STDATOMIC_H OR
    NOT HAVE_FORCE_ALIGN_ARG_POINTER_ATTR)
    set(ENABLE_BUNDLED_LIBUNWIND_DEFAULT OFF)
endif()
option(ENABLE_BUNDLED_LIBUNWIND "Bundled libunwind will be built"
       ${ENABLE_BUNDLED_LIBUNWIND_DEFAULT})

# On macOS there is no '-static-libstdc++' flag and it's use will
# raise following error:
# error: argument unused during compilation: '-static-libstdc++'
if(BUILD_STATIC AND NOT TARGET_OS_DARWIN)
    # Static linking for c++ routines
    add_compile_flags("C;CXX" "-static-libstdc++")
endif()

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

add_compile_flags("C;CXX" "-fno-common")

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
# Remove VALGRIND code and assertions in *any* type of release build.
    add_definitions("-DNDEBUG" "-DNVALGRIND")
endif()

option(ENABLE_WERROR "Make all compiler warnings into errors" OFF)

option(ENABLE_UB_SANITIZER "Make the compiler generate runtime code to perform undefined behaviour checks" OFF)

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
        add_definitions("-Doverride=")
    endif()

    add_compile_flags("C;CXX"
        "-Wall"
        "-Wextra"
    )
    if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_C_COMPILER_VERSION VERSION_LESS "8")
        # Strict aliasing violation warnings cannot be disabled for libev with
        # `system_header` pragma.
        add_compile_flags("C" "-Wno-strict-aliasing")
    endif()

    if (ENABLE_UB_SANITIZER)
        if (NOT CMAKE_COMPILER_IS_CLANG)
            message(FATAL_ERROR "Undefined behaviour sanitizer only available for clang")
        else()
            string(JOIN "," SANITIZE_FLAGS
                alignment bool bounds builtin enum float-cast-overflow
                float-divide-by-zero function integer-divide-by-zero return
                shift unreachable vla-bound
            )

            # Exclude "object-size".
            # Gives compilation warnings when -O0 is used, which is always,
            # because some tests build with -O0.

            # Exclude "pointer-overflow".
            # Stailq data structure subtracts a positive value from NULL.

            # Exclude "vptr".
            # Intrusive data structures may abuse '&obj->member' on pointer
            # 'obj' which is not really a pointer at an object of its type.
            # For example, rlist uses '&item->member' expression in macro cycles
            # to check end of cycle, but on the last iteration 'item' points at
            # the list metadata head, not at an object of type stored in this
            # list.

            # Exclude "implicit-signed-integer-truncation",
            # "implicit-integer-sign-change", "signed-integer-overflow".
            # Integer overflow and truncation are disabled due to extensive
            # usage of this UB in SQL code to 'implement' some kind of int65_t.

            # Exclude "null", "nonnull-attribute", "nullability-arg",
            # "returns-nonnull-attribute", "nullability-assign",
            # "nullability-return".
            # NULL checking is disabled, because this is not a UB and raises
            # lots of false-positive fails such as typeof(*obj) with
            # obj == NULL, or memcpy() with NULL argument and 0 size. All
            # nullability sanitations are disabled, because from the tests it
            # seems they implicitly turn each other on, when one is used. For
            # example, having "returns-nonnull-attribute" may lead to fail in
            # the typeof(*obj) when obj is NULL, even though there is nothing
            # related to return.

            set(SANITIZE_FLAGS "-fsanitize=${SANITIZE_FLAGS} -fno-sanitize-recover=${SANITIZE_FLAGS}")

            add_compile_flags("C;CXX" "${SANITIZE_FLAGS}")
        endif()
    endif()

    if (CMAKE_COMPILER_IS_CLANG OR CMAKE_COMPILER_IS_GNUCXX)
        # G++ bug. http://gcc.gnu.org/bugzilla/show_bug.cgi?id=31488
        # Also offsetof() is widely used in Tarantool source code
        # for classes and structs to implement intrusive lists and
        # some other data structures. G++ and clang++ both
        # complain about classes, having a virtual table. They
        # complain fair, but this can't be fixed for now.
        add_compile_flags("CXX"
            "-Wno-invalid-offsetof"
        )
        add_compile_flags("C;CXX" "-Wno-gnu-alignof-expression")
    endif()

    if (CMAKE_COMPILER_IS_GNUCC)
        # A workaround for Redhat Developer Toolset 2.x on RHEL/CentOS 5.x
        add_compile_flags("C" "-fno-gnu89-inline")
    endif()

    # Suppress noise GCC 8 warnings.
    #
    # reflection.h casts a pointer to a member function to an another pointer
    # to a member function to store it in a structure, but cast it back before
    # a call. It is legal and does not lead to an undefined behaviour.
    if (CC_HAS_WNO_CAST_FUNCTION_TYPE)
        add_compile_flags("C;CXX" "-Wno-cast-function-type")
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

    # Add -Werror if it is requested explicitly.
    if (ENABLE_WERROR)
        add_compile_flags("C;CXX" "-Werror")
    endif()
endmacro(enable_tnt_compile_flags)

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
    find_package_message(CC_BIT "Using slow implementation of bit operations"
        "${CMAKE_COMPILER_IS_CLANG}:${CMAKE_COMPILER_IS_GNUCC}")
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

if (CMAKE_CROSSCOMPILING)
    set(CMAKE_HOST_C_COMPILER cc)
    set(CMAKE_HOST_CXX_COMPILER c++)
else()
    set(CMAKE_HOST_C_COMPILER ${CMAKE_C_COMPILER})
    set(CMAKE_HOST_CXX_COMPILER ${CMAKE_CXX_COMPILER})
endif()
