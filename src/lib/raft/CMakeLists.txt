set(lib_sources
    raft.c
)

set_source_files_compile_flags(${lib_sources})
add_library(raft STATIC ${lib_sources})
target_link_libraries(raft core vclock)
