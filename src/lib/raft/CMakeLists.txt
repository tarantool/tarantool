set(lib_sources
    raft.c
    raft_ev.c
)

set_source_files_compile_flags(${lib_sources})

add_library(raft STATIC ${lib_sources})
target_link_libraries(raft core vclock)

# Algorithm library is for unit tests, and is not self-sufficient. In order to
# use it some other source file should define test symbols such as raft event
# loop utilities.
add_library(raft_algo STATIC raft.c)
target_link_libraries(raft_algo core vclock)
