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
