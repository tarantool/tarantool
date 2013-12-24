configure_file(
    "${PROJECT_SOURCE_DIR}/doc/tnt.ent.cmake"
    "${PROJECT_BINARY_DIR}/doc/tnt.ent"
    )

configure_file(
    "${PROJECT_SOURCE_DIR}/doc/www-data.in/download.cmake"
    "${PROJECT_BINARY_DIR}/doc/www-data.in/_text/download.md"
    )

add_subdirectory(man)

if (ENABLE_DOC)
    add_subdirectory(user)
    add_subdirectory(developer)
endif()


add_subdirectory(www-data.in EXCLUDE_FROM_ALL)
