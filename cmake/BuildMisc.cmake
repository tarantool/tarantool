#
# A macro to build the bundled libmisc
macro(libmisc_build)
    set(misc_src
        ${PROJECT_SOURCE_DIR}/third_party/PMurHash.c
        ${PROJECT_SOURCE_DIR}/third_party/base64.c
        ${PROJECT_SOURCE_DIR}/third_party/qsort_arg.c
    )

    if (NOT HAVE_MEMMEM)
        list(APPEND misc_src
            ${PROJECT_SOURCE_DIR}/third_party/memmem.c
        )
    endif()

    if (NOT HAVE_MEMRCHR)
        list(APPEND misc_src
            ${PROJECT_SOURCE_DIR}/third_party/memrchr.c
        )
    endif()

    if (NOT HAVE_CLOCK_GETTIME)
        list(APPEND misc_src
            ${PROJECT_SOURCE_DIR}/third_party/clock_gettime.c
        )
    endif()

    add_library(misc STATIC ${misc_src})
    set_target_properties(misc PROPERTIES COMPILE_FLAGS "${DEPENDENCY_CFLAGS}")

    unset(misc_src)
endmacro(libmisc_build)
