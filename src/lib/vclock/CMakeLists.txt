set(lib_sources
    vclock.c
)
set_source_files_compile_flags(${lib_sources})
add_library(vclock STATIC ${lib_sources})
target_link_libraries(vclock core bit)
