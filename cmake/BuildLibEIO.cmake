#
# A macro to build the bundled libeio
macro(libeio_build)
    set(eio_compile_flags)

    # See comments in BuildLibEV.cmake
    set(eio_compile_flags "${eio_compile_flags} -w")
    set(eio_compile_flags "${eio_compile_flags} -DENABLE_BUNDLED_LIBEIO=1")
    set(eio_compile_flags "${eio_compile_flags} -DEIO_STACKSIZE=0")
    if (TARGET_OS_LINUX)
        set(eio_compile_flags
            "${eio_compile_flags} -DHAVE_SYS_PRCTL_H -DHAVE_PRCTL_SET_NAME")
    endif ()

    set(eio_src
        ${PROJECT_SOURCE_DIR}/third_party/tarantool_eio.c
    )

    add_library(eio STATIC ${eio_src})
    target_link_libraries(eio pthread)

    set_target_properties(eio PROPERTIES COMPILE_FLAGS "${eio_compile_flags}")

    set(LIBEIO_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party)
    set(LIBEIO_LIBRARIES eio)

    unset(eio_src)
    unset(eio_compile_flags)
endmacro(libeio_build)
