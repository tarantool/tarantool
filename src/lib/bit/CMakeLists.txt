aux_source_directory(. lib_sources)
set_source_files_compile_flags(${lib_sources})
add_library(bit STATIC ${lib_sources})
