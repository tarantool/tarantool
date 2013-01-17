check_builtin_function_exists("__builtin_ctz" HAVE_BUILTIN_CTZ)
check_builtin_function_exists("__builtin_ctzll" HAVE_BUILTIN_CTZLL)
check_builtin_function_exists("__builtin_clz" HAVE_BUILTIN_CLZ)
check_builtin_function_exists("__builtin_clzll" HAVE_BUILTIN_CLZLL)
check_builtin_function_exists("__builtin_popcount" HAVE_BUILTIN_POPCOUNT)
check_builtin_function_exists("__builtin_popcountll" HAVE_BUILTIN_POPCOUNTLL)
check_builtin_function_exists("__builtin_bswap32" HAVE_BUILTIN_BSWAP32)
check_builtin_function_exists("__builtin_bswap64" HAVE_BUILTIN_BSWAP64)

if (NOT HAVE_BUILTIN_CTZ OR NOT HAVE_BUILTIN_CTZLL)
    # Check if -D_GNU_SOURCE has been defined and add this flag to
    # CMAKE_REQUIRED_DEFINITIONS in order to get check_prototype_definition work
    get_property(var DIRECTORY PROPERTY COMPILE_DEFINITIONS)
    list(FIND var "_GNU_SOURCE" var)
    if (NOT var EQUAL -1)
        set(CMAKE_REQUIRED_FLAGS "-Wno-error")
        set(CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
        check_prototype_definition("ffsl" "int ffsl(long int i);" "0"
             "string.h;strings.h" HAVE_FFSL)
        check_prototype_definition("ffsll" "int ffsll(long long int i);" "0"
             "string.h;strings.h" HAVE_FFSLL)
    endif()
endif()

if ((HAVE_BUILTIN_CTZ OR HAVE_FFSL) AND
    (HAVE_BUILTIN_CTZLL OR HAVE_FFSLL) AND
    HAVE_BUILTIN_CLZ AND HAVE_BUILTIN_CLZLL AND
    HAVE_BUILTIN_POPCOUNT AND HAVE_BUILTIN_POPCOUNTLL AND
    HAVE_BUILTIN_BSWAP32 AND HAVE_BUILTIN_BSWAP64)
    message(STATUS "Using compiler version of bit operations")
else()
    message(WARNING "Using slow implementation of bit operations")
endif()

set(lib_sources
    bit.c
)

set_source_files_compile_flags(${lib_sources})
add_library(bit STATIC ${lib_sources})
