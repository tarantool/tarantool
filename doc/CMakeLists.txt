configure_file(
    "${PROJECT_SOURCE_DIR}/doc/tnt.ent.cmake"
    "${PROJECT_BINARY_DIR}/doc/tnt.ent"
    )

configure_file(
    "${PROJECT_SOURCE_DIR}/doc/www/content/newsite/download.yml.in"
    "${PROJECT_BINARY_DIR}/doc/www/content/newsite/download.yml"
    )

add_subdirectory(man)

if (ENABLE_DOC)
    add_subdirectory(user)
    add_subdirectory(sphinx)
    add_subdirectory(developer)
    add_subdirectory(www)
endif()


