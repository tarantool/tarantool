find_package(Sphinx REQUIRED)

#find_program(MKDIR mkdir)

set(SPHINX_BUILD_DIR "${PROJECT_BINARY_DIR}/doc/sphinx/_build/")
set(SPHINX_HTML_DIR  "${PROJECT_BINARY_DIR}/doc/www/output/doc/")

#add_custom_command(OUTPUT
#    ${MKDIR} -p ${SPHINX_BUILD_DIR})
#add_custom_command(OUTPUT
#    ${MKDIR} -p ${SPHINX_HTML_DIR})

add_custom_target(sphinx-html ALL
#    DEPENDS ${SPHINX_BUILD_DIR} ${SPHINX_HTML_DIR}
    COMMAND "${SPHINX_EXECUTABLE}"
        -b html
        -d "${SPHINX_BUILD_DIR}"
        -c html/
        "${PROJECT_SOURCE_DIR}/doc/sphinx"
        "${SPHINX_HTML_DIR}"
    COMMENT "Building HTML documentation with Sphinx"
)

add_custom_target(sphinx-singlehtml ALL
    COMMAND "${SPHINX_EXECUTABLE}"
        -b singlehtml
        -d "${SPHINX_BUILD_DIR}"
        -c singlehtml/
        "${PROJECT_SOURCE_DIR}/doc/sphinx"
        "${SPHINX_HTML_DIR}"
    COMMENT "Building HTML documentation with Sphinx"
)
