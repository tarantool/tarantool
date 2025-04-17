include(cmake/utils.cmake)

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
if (CMAKE_C_COMPILER_ID STREQUAL Clang OR
    CMAKE_C_COMPILER_ID STREQUAL AppleClang)
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
# C++17 mode is the default since GCC 11; it can be explicitly selected with
# the -std=c++17 command-line flag, or -std=gnu++17 to enable GNU extensions
# as well. Some C++17 features are available since GCC 5, but support was
# experimental and the ABI of C++17 features was not stable until GCC 9 [1].
#
# Clang 5 and later implement all the features of the ISO C++ 2017 standard.
# By default, Clang 16 or later builds C++ code according to the C++17
# standard [2].
#
# 1. https://gcc.gnu.org/projects/cxx-status.html#cxx17
# 2. https://clang.llvm.org/cxx_status.html#cxx17
#
set(CMAKE_REQUIRED_FLAGS "-std=c11")
check_c_source_compiles("int main(void) { return 0; }" HAVE_STD_C11)
set(CMAKE_REQUIRED_FLAGS "-std=c++17")
check_cxx_source_compiles("int main(void) { return 0; }" HAVE_STD_CXX17)
set(CMAKE_REQUIRED_FLAGS "")
if(NOT HAVE_STD_C11 OR NOT HAVE_STD_CXX17)
    message (FATAL_ERROR
        "${CMAKE_C_COMPILER} should support -std=c11. "
        "${CMAKE_CXX_COMPILER} should support -std=c++17. "
        "Please consider upgrade to gcc 9+ or clang 5+.")
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
# Disabled backtraces support on AArch64 Linux by default, because of gh-8791.
if(NOT TARGET_OS_FREEBSD AND HAVE_FORCE_ALIGN_ARG_POINTER_ATTR AND HAVE_CFI_ASM
        AND NOT (TARGET_OS_LINUX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"))
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

# In Clang there is no '-static-libstdc++' flag and its use will raise
# the following error:
#     clang: error: argument unused during compilation: '-static-libstdc++'
if(BUILD_STATIC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
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

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions("-Wp,-U_FORTIFY_SOURCE")
else()
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
    add_compile_flags("C" "-std=c11")
    add_compile_flags("CXX" "-std=c++17")

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
        # UndefinedBehaviourSanitizer has been added to GCC since
        # version 4.9.0, see https://gcc.gnu.org/gcc-4.9/changes.html.
        if(CMAKE_COMPILER_IS_GNUCC AND
            CMAKE_C_COMPILER_VERSION VERSION_LESS 4.9.0)
            message(FATAL_ERROR "UndefinedBehaviourSanitizer is unsupported in GCC ${CMAKE_C_COMPILER_VERSION}")
        endif()
        # Use all needed checks from the UndefinedBehaviorSanitizer
        # documentation:
        # https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html.
        string(JOIN "," UBSAN_IGNORE_OPTIONS
            # Gives compilation warnings when -O0 is used, which is always,
            # because some tests build with -O0.
            object-size
            # See https://github.com/tarantool/tarantool/issues/10742.
            pointer-overflow
            # NULL checking is disabled, because this is not a UB and raises
            # lots of false-positive fails such as typeof(*obj) with
            # obj == NULL, or memcpy() with NULL argument and 0 size.
            # "UBSan: check null is globally suppressed",
            # https://github.com/tarantool/tarantool/issues/10741
            null
            # "UBSan: check nonnull-attribute is globally suppressed",
            # https://github.com/tarantool/tarantool/issues/10740
            nonnull-attribute
        )
        # GCC has no "function" UB check. See details here:
        # https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html#index-fsanitize_003dundefined
        if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
            string(JOIN "," UBSAN_IGNORE_OPTIONS
                ${UBSAN_IGNORE_OPTIONS}
                # Not interested in function type mismatch errors.
                function
            )
        endif()
        # XXX: To get nicer stack traces in error messages.
        set(SANITIZE_FLAGS "${SANITIZE_FLAGS} -fno-omit-frame-pointer")
        # Enable UndefinedBehaviorSanitizer support.
        # This flag enables all supported options (the documentation
        # on site is not correct about that moment, unfortunately)
        # except float-divide-by-zero. Floating point division by zero
        # behaviour is defined without -ffast-math and uses the
        # IEEE 754 standard on which all NaN tagging is based.
        set(SANITIZE_FLAGS "${SANITIZE_FLAGS} -fsanitize=undefined")
        set(SANITIZE_FLAGS "${SANITIZE_FLAGS} -fno-sanitize=${UBSAN_IGNORE_OPTIONS}")
        # Print a verbose error report and exit the program.
        set(SANITIZE_FLAGS "${SANITIZE_FLAGS} -fno-sanitize-recover=undefined")
        add_compile_flags("C;CXX" "${SANITIZE_FLAGS}")
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
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
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
