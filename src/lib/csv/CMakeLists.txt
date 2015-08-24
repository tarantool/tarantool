set(lib_sources
    csv.c
)

set_source_files_compile_flags(${lib_sources})
add_library(csv STATIC ${lib_sources})
