set(lib_sources
    bitset.c
    page.c
    expr.c
    iterator.c
    index.c
)

set_source_files_compile_flags(${lib_sources})
add_library(bitset STATIC ${lib_sources})
target_link_libraries(bitset bit)
