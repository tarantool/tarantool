# A macro to build the bundled sophia library
#
macro(sophia_build)
    set(SOPHIA_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/sophia/db)

    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
		add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/third_party/sophia/db/libsophia.a
			WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/sophia
			COMMAND $(MAKE) -C db libsophia.a
			DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
		)
    else()
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/sophia
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/third_party/sophia
        )
		add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/sophia/db/libsophia.a
			WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/sophia
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/third_party/sophia ${PROJECT_BINARY_DIR}/third_party/sophia
			COMMAND $(MAKE) -C db libsophia.a
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${PROJECT_BINARY_DIR}/third_party/sophia
		)
    endif()

	add_custom_target(libsophia ALL
		DEPENDS ${PROJECT_BINARY_DIR}/third_party/sophia/db/libsophia.a
	)

    message(STATUS "Use bundled sophia: ${SOPHIA_INCLUDE_DIR}")
endmacro(sophia_build)
