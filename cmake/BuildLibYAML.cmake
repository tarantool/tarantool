#
# A macro to build the bundled libyaml
macro(libyaml_build)
    set(LIBYAML_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/third_party/libyaml/include)
    set(LIBYAML_LIBRARIES yaml)

    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/libyaml EXCLUDE_FROM_ALL)
    # See comments in BuildLibEV.cmake
    set_target_properties(yaml PROPERTIES COMPILE_FLAGS "-w")

    find_package_message(LIBYAML
        "Using bundled libyaml"
        "${LIBYAML_LIBRARIES}:${LIBYAML_INCLUDE_DIRS}")

    unset(yaml_src)
endmacro(libyaml_build)

