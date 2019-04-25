set(lib_sources crypto.c)

set_source_files_compile_flags(${lib_sources})
add_library(crypto STATIC ${lib_sources})
target_link_libraries(crypto ${OPENSSL_LIBRARIES} core)
