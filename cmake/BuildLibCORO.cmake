#
# A macro to build the bundled libcoro
macro(libcoro_build)
    set(coro_src
        ${PROJECT_SOURCE_DIR}/third_party/coro/coro.c
    )

    add_library(coro STATIC ${coro_src})

    set(LIBCORO_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/coro)
    set(LIBCORO_LIBRARIES coro)

    if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "amd64")
        add_definitions("-DCORO_ASM")
    elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
        add_definitions("-DCORO_ASM")
    else()
        add_definitions("-DCORO_SJLJ")
    endif()

    unset(coro_src)
endmacro(libcoro_build)

