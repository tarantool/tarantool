enable_tnt_compile_flags()

add_compile_flags("C;CXX"
    "-Wno-unused-parameter")

add_custom_target(test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var)

add_custom_target(test-force
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --force --vardir=${PROJECT_BINARY_DIR}/test/var)

add_subdirectory(unit)
add_subdirectory(connector_c)

if (ENABLE_RPM)
    install (FILES ${CMAKE_SOURCE_DIR}/test/share/tarantool_rpm.cfg
             DESTINATION ${CMAKE_SYSCONF_DIR}/tarantool/
             RENAME "tarantool.cfg")
    install (FILES ${CMAKE_SOURCE_DIR}/src/bootstrap.snap
             DESTINATION share/tarantool
             RENAME 00000000000000000001.snap)
else()
    install (FILES ${CMAKE_SOURCE_DIR}/test/share/tarantool_tgz.cfg
             DESTINATION "${CMAKE_SYSCONF_DIR}"
             RENAME "tarantool.cfg")
    install (FILES ${CMAKE_SOURCE_DIR}/src/bootstrap.snap
             DESTINATION "${CMAKE_LOCALSTATE_DIR}/lib/tarantool"
             RENAME 00000000000000000001.snap)
endif()
