#
# A macro to build the bundled libcoro
macro(libcoro_build)
    set(coro_src
        ${PROJECT_SOURCE_DIR}/third_party/coro/coro.c
    )

    add_library(coro STATIC ${coro_src})

    set(LIBCORO_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/coro)
    set(LIBCORO_LIBRARIES coro)

    message(STATUS "Use bundled libcoro includes: ${LIBCORO_INCLUDE_DIR}/coro.h")
    message(STATUS "Use bundled libcoro library: ${LIBCORO_LIBRARIES}")

    unset(coro_src)
endmacro(libcoro_build)

