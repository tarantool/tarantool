#
# A macro to build the bundled libmisc
macro(libmisc_build)
    set(misc_src
        ${PROJECT_SOURCE_DIR}/third_party/sha1.c
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

    if (HAVE_OPENMP)
        list(APPEND misc_src
             ${PROJECT_SOURCE_DIR}/third_party/qsort_arg_mt.c)
    endif()

    add_library(misc STATIC ${misc_src})

    if (HAVE_OPENMP)
        if(BUILD_STATIC)
            set(GOMP_LIBRARY libgomp.a)
        else()
            set(GOMP_LIBRARY gomp)
        endif()
        target_link_libraries(misc ${GOMP_LIBRARY} pthread ${CMAKE_DL_LIBS})
    endif()

    unset(misc_src)
endmacro(libmisc_build)
