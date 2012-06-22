
#
# List generators
#
if ("${CPACK_GENERATOR}" STREQUAL "RPM")
    set (CPACK_RPM_PACKAGE_NAME "tarantool")
    set (CPACK_RPM_PACKAGE_SUMMARY "tarantool")
    set (CPACK_RPM_PACKAGE_VERSION "${TARANTOOL_VERSION}")
    set (CPACK_RPM_PACKAGE_RELEASE "8")
    set (CPACK_RPM_PACKAGE_LICENSE "BSD")
    set (CPACK_RPM_PACKAGE_VENDOR "MAIL.RU")
    set (CPACK_RPM_PACKAGE_DESCRIPTION "
Tarantool/Box, or simply Tarantool, is a high performance
in-memory NoSQL database. It supports replication, online backup,
stored procedures in Lua.")
    set (CPACK_RPM_PACKAGE_SUMMARY "Tarantool/Box - an efficient in-memory data store")
    set (CPACK_RPM_PACKAGE_GROUP "Databases")
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
    "${CMAKE_SOURCE_DIR}/connector/c/include/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/c/tnt/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/c/tntnet/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/c/tntsql/Makefile"
    "${CMAKE_SOURCE_DIR}/connector/c/tntrp/Makefile"
    "${CMAKE_SOURCE_DIR}/mod/Makefile"
    "${CMAKE_SOURCE_DIR}/mod/box/Makefile"
    "${CMAKE_SOURCE_DIR}/cfg/Makefile"
    "${CMAKE_SOURCE_DIR}/src/Makefile"
    "${CMAKE_SOURCE_DIR}/extra/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/user/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/developer/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/man/Makefile"
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
