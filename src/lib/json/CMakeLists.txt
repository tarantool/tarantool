set(lib_sources
    json.c
)

set_source_files_compile_flags(${lib_sources})
add_library(json STATIC ${lib_sources})
target_link_libraries(json misc)
