set(lib_sources base64.c crc32.c crc32_impl.c crc32_hw_impl.c PMurHash.c)
set_source_files_compile_flags(${lib_sources})
add_library(digest STATIC ${lib_sources})
