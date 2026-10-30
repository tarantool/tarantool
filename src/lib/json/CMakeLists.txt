set(lib_sources
    json.c
)

set_source_files_compile_flags(${lib_sources})
add_library(json STATIC ${lib_sources})
target_link_libraries(json misc)

if (ENABLE_BUNDLED_ICU)
    add_dependencies(json bundled-icu)
endif()
