set(lib_sources rope.c rtree.c guava.c bloom.c)
set_source_files_compile_flags(${lib_sources})
add_library(salad STATIC ${lib_sources})
