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

    add_library(yaml STATIC ${yaml_src})

    set(LIBYAML_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/libyaml)
    set(LIBYAML_LIBRARIES yaml)

    # A workaround for config.h
    set_target_properties(yaml PROPERTIES  COMPILE_DEFINITIONS "HAVE_CONFIG_H")
    include_directories(${LIBYAML_INCLUDE_DIR})

    message(STATUS "Use bundled libyaml library")
    unset(yaml_src)
endmacro(libyaml_build)

