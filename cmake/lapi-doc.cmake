#
# Build the Lua API documentation from the generated EmmyLua stubs.
#
# The `emmylua-stubs` target (see cmake/LuaStub.cmake) extracts the
# public EmmyLua annotations of the `checks` and `metrics` libraries
# into `${PROJECT_BINARY_DIR}/emmylua`. `emmylua_doc_cli` generates
# the documentation in HTML format from those stubs.
#
# The HTML documentation is generated in the
# `${PROJECT_BINARY_DIR}/lapi-doc/site` directory and can be built
# with the `lapi` target, which builds `emmylua-stubs` first.
#
# The target requires `emmylua_doc_cli` installed and is not built
# by default.

find_program(EMMYLUA_DOC_CLI emmylua_doc_cli)

set(LAPI_ANNOTATIONS
    "${PROJECT_BINARY_DIR}/emmylua/checks.lua"
    "${PROJECT_BINARY_DIR}/emmylua/metrics.lua"
)

if(NOT EMMYLUA_DOC_CLI)
    message(STATUS
        "The 'lapi' target is disabled: emmylua_doc_cli is required to build "
        "the Lua API documentation")
    return()
endif()

set(LAPI_SITE_DIR "${PROJECT_BINARY_DIR}/lapi-doc/site")

add_custom_command(
    OUTPUT "${LAPI_SITE_DIR}/index.html"
    COMMAND ${EMMYLUA_DOC_CLI}
            ${LAPI_ANNOTATIONS}
            --output "${LAPI_SITE_DIR}"
            --output-format html
            --site-name "Tarantool Lua API"
    DEPENDS ${LAPI_ANNOTATIONS}
    COMMENT "Generate Tarantool Lua API documentation in HTML"
    VERBATIM)

add_custom_target(lapi DEPENDS "${LAPI_SITE_DIR}/index.html")
add_dependencies(lapi emmylua-stubs)
