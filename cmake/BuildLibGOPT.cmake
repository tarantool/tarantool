#
# A macro to build the bundled libgopt
macro(libgopt_build)

    set(gopt_src ${PROJECT_SOURCE_DIR}/third_party/gopt/gopt.c)

    add_library(gopt STATIC ${gopt_src})

    set(LIBGOPT_INCLUDE_DIR ${PROJECT_BINARY_DIR}/third_party/gopt)
    set(LIBGOPT_LIBRARIES gopt)

    message(STATUS "Use bundled libgopt includes: ${LIBGOPT_INCLUDE_DIR}/gopt.h")
    message(STATUS "Use bundled libgopt library: ${LIBGOPT_LIBRARIES}")

    unset(gopt_src)
endmacro(libgopt_build)

