# Generate HTML documentation for the public C API using Doxygen.
#
# The `capi-doc` target runs Doxygen over the public API header
# (${PROJECT_BINARY_DIR}/src/module.h, built by the `api` target) and writes
# the result into `${PROJECT_BINARY_DIR}/doc/capi-site`. The target is
# disabled with a STATUS message when Doxygen is not found and is not built
# by default.
find_package(Doxygen)
if(NOT DOXYGEN_FOUND)
    message(STATUS
        "The 'capi-doc' target is disabled: doxygen is required to build "
        "the C API documentation")
    return()
endif()

configure_file(
    "${PROJECT_SOURCE_DIR}/Doxyfile.API.in"
    "${PROJECT_BINARY_DIR}/Doxyfile.API"
    @ONLY)

set(CAPI_SITE_DIR "${PROJECT_BINARY_DIR}/doc/capi-site")

add_custom_command(
    OUTPUT "${CAPI_SITE_DIR}/index.html"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CAPI_SITE_DIR}"
    COMMAND ${DOXYGEN_EXECUTABLE} "${PROJECT_BINARY_DIR}/Doxyfile.API"
    DEPENDS api "${PROJECT_BINARY_DIR}/Doxyfile.API"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    COMMENT "Generate Tarantool C API documentation in HTML"
    VERBATIM)

add_custom_target(capi-doc DEPENDS "${CAPI_SITE_DIR}/index.html")
add_dependencies(capi-doc api)
