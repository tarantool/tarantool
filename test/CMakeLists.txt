add_custom_target(test
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var)

add_custom_target(test-force
    COMMAND python ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --force --vardir=${PROJECT_BINARY_DIR}/test/var)

add_subdirectory(box)
add_subdirectory(connector_c)

if ("${CPACK_GENERATOR}" STREQUAL "RPM")
	install (FILES ${CMAKE_SOURCE_DIR}/test/box/tarantool.cfg
		 DESTINATION etc)
	install (FILES ${CMAKE_SOURCE_DIR}/test/box/00000000000000000001.snap
		 DESTINATION share/tarantool)
else()
	install (FILES ${CMAKE_SOURCE_DIR}/test/box/tarantool.cfg
		DESTINATION bin)
	install (FILES ${CMAKE_SOURCE_DIR}/test/box/00000000000000000001.snap
		DESTINATION bin)
endif()
