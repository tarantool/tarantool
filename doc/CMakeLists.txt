configure_file(
    "${PROJECT_SOURCE_DIR}/doc/www/content/newsite/download.yml.in"
    "${PROJECT_BINARY_DIR}/doc/www/content/newsite/download.yml"
    )

add_subdirectory(man)

if (ENABLE_DOC)
    add_subdirectory(sphinx)
    add_subdirectory(www)
endif()
