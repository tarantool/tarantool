find_package(Sphinx REQUIRED)
find_package(Doxygen REQUIRED)
#find_program(MKDIR mkdir)

configure_file("${PROJECT_SOURCE_DIR}/Doxyfile.API.in"
    "${PROJECT_BINARY_DIR}/Doxyfile.API")

add_custom_target(api-doc
    COMMAND ${DOXYGEN_EXECUTABLE} "${PROJECT_BINARY_DIR}/Doxyfile.API"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    DEPENDS api
    COMMENT "Generating API documentation" VERBATIM)

set(SPHINX_BUILD_SINGLE_DIR "${PROJECT_BINARY_DIR}/doc/sphinx/_single_build/")
set(SPHINX_BUILD_HTML_DIR   "${PROJECT_BINARY_DIR}/doc/sphinx/_html_build/")
set(SPHINX_HTML_DIR  "${PROJECT_BINARY_DIR}/doc/www/output/doc/")

#add_custom_command(OUTPUT
#    ${MKDIR} -p ${SPHINX_BUILD_DIR})
#add_custom_command(OUTPUT
#    ${MKDIR} -p ${SPHINX_HTML_DIR})

add_custom_target(sphinx-html ALL
#    DEPENDS ${SPHINX_BUILD_DIR} ${SPHINX_HTML_DIR}
    COMMAND "${SPHINX_EXECUTABLE}"
        -b html
        -d "${SPHINX_BUILD_SINGLE_DIR}"
        -c html/
        "${PROJECT_SOURCE_DIR}/doc/sphinx"
        "${SPHINX_HTML_DIR}"
    COMMENT "Building HTML documentation with Sphinx"
    DEPENDS api-doc
)

add_custom_target(sphinx-singlehtml ALL
    COMMAND "${SPHINX_EXECUTABLE}"
        -b singlehtml
        -d "${SPHINX_BUILD_HTML_DIR}"
        -c singlehtml/
        "${PROJECT_SOURCE_DIR}/doc/sphinx"
        "${SPHINX_HTML_DIR}"
        singlehtml.rst
    COMMENT "Building HTML documentation with Sphinx"
    DEPENDS api-doc
)
