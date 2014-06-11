enable_tnt_compile_flags()

add_compile_flags("C;CXX"
    "-Wno-unused-parameter")

add_custom_target(test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var)

add_custom_target(test-force
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --force --vardir=${PROJECT_BINARY_DIR}/test/var)


add_subdirectory(unit)
# Disable connector_c for 1.6
#add_subdirectory(connector_c)

if (ENABLE_RPM)
    install (FILES ${CMAKE_SOURCE_DIR}/src/box/bootstrap.snap
             DESTINATION ${CMAKE_INSTALL_DATADIR}/tarantool
             RENAME 00000000000000000001.snap)
else()
    install (FILES ${CMAKE_SOURCE_DIR}/src/box/bootstrap.snap
             DESTINATION "${CMAKE_INSTALL_LOCALSTATEDIR}/lib/tarantool"
             RENAME 00000000000000000001.snap)
endif()
