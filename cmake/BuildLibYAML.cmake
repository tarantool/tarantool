#
# A macro to build the bundled libyaml
macro(libyaml_build)
    set(yaml_src ${PROJECT_SOURCE_DIR}/third_party/libyaml/api.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/dumper.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/emitter.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/loader.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/parser.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/reader.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/scanner.c
        ${PROJECT_SOURCE_DIR}/third_party/libyaml/writer.c)

    set(LIBYAML_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/third_party/libyaml)
    set(LIBYAML_LIBRARIES yaml)

    add_library(yaml STATIC ${yaml_src})
    set(yaml_compile_flags -Wno-unused)
    if (CC_HAS_WNO_PARENTHESES_EQUALITY)
        set(yaml_compile_flags "${yaml_compile_flags} -Wno-parentheses-equality")
    endif()
    set(yaml_compile_flags "${yaml_compile_flags} -I${LIBYAML_INCLUDE_DIRS}")
    set_target_properties(yaml PROPERTIES COMPILE_FLAGS "${yaml_compile_flags}")

    # A workaround for config.h
    set_target_properties(yaml PROPERTIES COMPILE_DEFINITIONS "HAVE_CONFIG_H")

    find_package_message(LIBYAML
        "Using bundled libyaml"
        "${LIBYAML_LIBRARIES}:${LIBYAML_INCLUDE_DIRS}")

    unset(yaml_src)
endmacro(libyaml_build)

