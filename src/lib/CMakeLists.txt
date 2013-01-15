include_directories("${PROJECT_SOURCE_DIR}/include/lib")

add_custom_target(libs)

add_subdirectory(bit)
add_dependencies(libs bit)

