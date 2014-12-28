set(lib_sources rope.c rlist.c rtree.c guava.c)
set_source_files_compile_flags(${lib_sources})
add_library(salad ${lib_sources})
