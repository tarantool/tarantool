set(lib_sources slab_cache.c region.c mempool.c slab_arena.c small.c pt_alloc.c)
set_source_files_compile_flags(${lib_sources})
add_library(small ${lib_sources})
