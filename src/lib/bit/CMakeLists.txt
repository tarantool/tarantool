set(lib_sources
    bit.c
)

set_source_files_compile_flags(${lib_sources})
add_library(bit STATIC ${lib_sources})
