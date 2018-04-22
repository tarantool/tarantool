set(lib_sources
    path.c
)

set_source_files_compile_flags(${lib_sources})
add_library(json_path STATIC ${lib_sources})
