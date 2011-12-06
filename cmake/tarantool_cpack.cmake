
#
# List generators
#
if ("${CPACK_GENERATOR}" STREQUAL "RPM")
    set (CPACK_RPM_PACKAGE_NAME "tarantool_box")
    set (CPACK_RPM_PACKAGE_SUMMARY "tarantool_box")
    execute_process (COMMAND "date" "+%Y%m%d.%H%M" OUTPUT_VARIABLE RPM_PACKAGE_VERSION)
    set (CPACK_RPM_PACKAGE_VERSION "${RPM_PACKAGE_VERSION}")
    set (CPACK_RPM_PACKAGE_RELEASE "8")
    set (CPACK_RPM_PACKAGE_LICENSE "BSD")
    set (CPACK_RPM_PACKAGE_GROUP "MAIL.RU")
    set (CPACK_RPM_PACKAGE_DESCRIPTION "Tarantool in-memory DB storage")
    set (CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/cmake/rpm_post_install.sh")
    set (CPACK_SET_DESTDIR "ON") 
    set (CPACK_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
else()
    set (CPACK_GENERATOR "TGZ")
endif()

set (CPACK_SOURCE_GENERATOR "TGZ")

#
# Ignoring generated files
#
set (CPACK_SOURCE_IGNORE_FILES
    "\\\\.git" "_CPack_Packages"
    "CMakeCache.txt" "CPackSourceConfig.cmake" "CPackConfig.cmake"
    "CMakeFiles" "\\\\.gz" "\\\\.Z" "\\\\.zip" "\\\\.rpm"
    "\\\\.o" "\\\\.so" "\\\\.a"
    "client/tarantool/tarantool"
    "mod/box/tarantool_box"
    "mod/box/tarantool_feeder"
    "install_manifest.txt"
    "cmake_install.cmake" "test/var/"
)

set (CPACK_SOURCE_IGNORE_FILES "${CPACK_SOURCE_IGNORE_FILES}"
    "${CMAKE_SOURCE_DIR}/Makefile"
    "${CMAKE_SOURCE_DIR}/test/Makefile"
    "${CMAKE_SOURCE_DIR}/test/lib/Makefile"
    "${CMAKE_SOURCE_DIR}/client/Makefile"
    "${CMAKE_SOURCE_DIR}/client/tarantool/Makefile"
    "${CMAKE_SOURCE_DIR}/third_party/Makefile"
    "${CMAKE_SOURCE_DIR}/third_party/gopt/Makefile"
    "${CMAKE_SOURCE_DIR}/third_party/memcached/doc/Makefile"
    "${CMAKE_SOURCE_DIR}/third_party/coro/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/c/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/c/sql/Makefile"
    "${CMAKE_SOURCE_DIR}/mod/Makefile"
    "${CMAKE_SOURCE_DIR}/mod/box/Makefile"
    "${CMAKE_SOURCE_DIR}/cfg/Makefile"
    "${CMAKE_SOURCE_DIR}/core/Makefile"
    "${CMAKE_SOURCE_DIR}/extra/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/user/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/developer/Makefile"
)

set (CPACK_SOURCE_PACKAGE_FILE_NAME "tarantool-${TARANTOOL_VERSION}-src")

#
# Provide options for the binary distribution.
#
string (TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" platform)

set (CPACK_PACKAGE_FILE_NAME "tarantool-${TARANTOOL_VERSION}-${platform}")
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-debug")
endif()

##
include (CPack)
