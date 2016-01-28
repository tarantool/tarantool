# A macro to build the bundled sophia library
#
macro(sophia_build)
    set(SOPHIA_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/sophia/sophia/sophia)
    set(SOPHIA_OPTS
        CFLAGS="${CMAKE_C_FLAGS}"
        LDFLAGS="${CMAKE_SHARED_LINKER_FLAGS}")
    separate_arguments(SOPHIA_OPTS)
    set(SOPHIA_DIR "${PROJECT_BINARY_DIR}/third_party/sophia")
    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
        add_custom_command(OUTPUT ${SOPHIA_DIR}/libsophia.a
            WORKING_DIRECTORY ${SOPHIA_DIR}
            COMMAND $(MAKE) ${SOPHIA_OPTS} clean
            COMMAND $(MAKE) ${SOPHIA_OPTS} static
            DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
            )
    else()
        add_custom_command(OUTPUT ${SOPHIA_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SOPHIA_DIR}
        )
        add_custom_command(OUTPUT ${SOPHIA_DIR}/libsophia.a
            WORKING_DIRECTORY ${SOPHIA_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${PROJECT_SOURCE_DIR}/third_party/sophia
                ${SOPHIA_DIR}
            COMMAND $(MAKE) ${SOPHIA_OPTS} clean
            COMMAND $(MAKE) ${SOPHIA_OPTS} static
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${SOPHIA_DIR}
        )
    endif()

    add_custom_target(libsophia ALL DEPENDS ${SOPHIA_DIR}/libsophia.a)
    add_dependencies(build_bundled_libs libsophia)

    set(SOPHIA_LIBRARIES ${SOPHIA_DIR}/libsophia.a)
    set(SOPHIA_INCLUDE_DIRS ${SOPHIA_INCLUDE_DIR})
endmacro(sophia_build)
