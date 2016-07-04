include(FindPackageHandleStandardArgs)

find_program(SPHINX_EXECUTABLE NAMES sphinx-build
    HINTS
    $ENV{SPHINX_DIR}
    PATH_SUFFIXES bin
)

find_program(SPHINX_INTL_EXECUTABLE NAMES sphinx-intl
    HINTS
    $ENV{SPHINX_INTL_DIR}
    PATH_SUFFIXES bin
)

find_package_handle_standard_args(Sphinx DEFAULT_MSG SPHINX_EXECUTABLE SPHINX_INTL_EXECUTABLE)
mark_as_advanced(SPHINX_EXECUTABLE SPHINX_INTL_EXECUTABLE)
