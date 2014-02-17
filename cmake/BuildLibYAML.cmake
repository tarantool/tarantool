#
# A macro to build the bundled liblua-yaml
macro(libyaml_build)
    set(yaml_src ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/lyaml.cc
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/api.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/dumper.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/emitter.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/loader.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/parser.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/reader.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/scanner.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/writer.c
	             ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/b64.c)

    set_source_files_properties(${yaml_src} PROPERTIES COMPILE_FLAGS
        "-std=c99")
    set_source_files_properties(
        ${PROJECT_SOURCE_DIR}/third_party/lua-yaml/lyaml.cc
        PROPERTIES COMPILE_FLAGS
        "-std=gnu++0x -D__STDC_FORMAT_MACROS=1 -D__STDC_LIMIT_MACROS=1")

    add_library(yaml STATIC ${yaml_src})

    set(LIBYAML_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/lua-yaml)
    set(LIBYAML_LIBRARIES yaml)

    message(STATUS "Use bundled Lua-YAML library: ${LIBYAML_LIBRARIES}")

    unset(lua_yaml_src)
endmacro(libyaml_build)

