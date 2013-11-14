#
# A macro to build the bundled libmisc
macro(libmisc_build)
    set(misc_src
        ${PROJECT_SOURCE_DIR}/third_party/crc32.c
        ${PROJECT_SOURCE_DIR}/third_party/proctitle.c
        ${PROJECT_SOURCE_DIR}/third_party/PMurHash.c
        ${PROJECT_SOURCE_DIR}/third_party/base64.c
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

    if (NOT HAVE_OPEN_MEMSTREAM)
        list(APPEND misc_src
            ${PROJECT_SOURCE_DIR}/third_party/open_memstream.c
        )
    endif()
    if (NOT TARGET_OS_DEBIAN_FREEBSD)
        if (TARGET_OS_FREEBSD)
            set_source_files_properties(
            ${PROJECT_SOURCE_DIR}/third_party/proctitle.c
            PROPERTIES COMPILE_FLAGS "-DHAVE_SETPROCTITLE")
        endif()
    endif()

    add_library(misc STATIC ${misc_src})

    unset(misc_src)
endmacro(libmisc_build)
