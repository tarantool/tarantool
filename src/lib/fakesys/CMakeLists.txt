set(lib_sources
    fakenet.c
    fakeev.c
)

set_source_files_compile_flags(${lib_sources})
add_library(fakesys STATIC ${lib_sources})
target_link_libraries(fakesys core)
