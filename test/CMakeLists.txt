enable_tnt_compile_flags()

add_compile_flags("C;CXX"
    "-Wno-unused-parameter")

add_custom_target(test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --vardir=${PROJECT_BINARY_DIR}/test/var)

add_custom_target(test-force
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --builddir=${PROJECT_BINARY_DIR} --force --vardir=${PROJECT_BINARY_DIR}/test/var)

add_subdirectory(unit)
add_subdirectory(box)
add_subdirectory(connector_c)

macro(install_cfg type_cfg dest)
    install (FILES ${CMAKE_SOURCE_DIR}/test/share/tarantool_${type_cfg}.cfg
             DESTINATION ${dest}
             RENAME "tarantool.cfg")
endmacro(install_cfg)

if (ENABLE_RPM)
    install_cfg(rpm ${CMAKE_SYSCONF_DIR}/tarantool/)
    install (FILES ${CMAKE_SOURCE_DIR}/test/box/00000000000000000001.snap
             DESTINATION share/tarantool)
else()
    if (TARGET_OS_DARWIN)
        install_cfg(dmg ${CMAKE_SYSCONF_DIR})
    else()
        install_cfg(tgz ${CMAKE_SYSCONF_DIR})
    endif()

    install (FILES ${CMAKE_SOURCE_DIR}/test/box/00000000000000000001.snap
             DESTINATION "${CMAKE_LOCALSTATE_DIR}/lib/tarantool")
endif()
