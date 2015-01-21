# A macro to build the bundled sophia library
#
macro(sophia_build)
    set(SOPHIA_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/sophia/sophia/sophia)
	set(SOPHIA_OPTS
	    CFLAGS="${CMAKE_C_FLAGS}"
	    LDFLAGS="${CMAKE_SHARED_LINKER_FLAGS}")
	separate_arguments(SOPHIA_OPTS)
    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
		add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/third_party/sophia/libsophia.a
			WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/sophia
			COMMAND $(MAKE) ${SOPHIA_OPTS} clean
			COMMAND $(MAKE) ${SOPHIA_OPTS} static
			DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
		)
    else()
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/sophia
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/third_party/sophia
        )
		add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/sophia/libsophia.a
			WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/sophia
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/third_party/sophia ${PROJECT_BINARY_DIR}/third_party/sophia
			COMMAND $(MAKE) ${SOPHIA_OPTS} clean
			COMMAND $(MAKE) ${SOPHIA_OPTS} static
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${PROJECT_BINARY_DIR}/third_party/sophia
		)
    endif()
	add_custom_target(libsophia ALL
		DEPENDS ${PROJECT_BINARY_DIR}/third_party/sophia/libsophia.a
	)
    message(STATUS "Use bundled Sophia: ${PROJECT_SOURCE_DIR}/third_party/sophia/")
    set (sophia_lib "${PROJECT_BINARY_DIR}/third_party/sophia/libsophia.a")
    add_dependencies(build_bundled_libs libsophia)
endmacro(sophia_build)
