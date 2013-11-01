
#
# List generators
#
set (CPACK_GENERATOR "TGZ")
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
    "src/box/tarantool_box"
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
    "${CMAKE_SOURCE_DIR}/src/Makefile"
    "${CMAKE_SOURCE_DIR}/src/box/Makefile"
    "${CMAKE_SOURCE_DIR}/cfg/Makefile"
    "${CMAKE_SOURCE_DIR}/src/Makefile"
    "${CMAKE_SOURCE_DIR}/extra/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/user/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/developer/Makefile"
    "${CMAKE_SOURCE_DIR}/doc/man/Makefile"
)

set (CPACK_SOURCE_PACKAGE_FILE_NAME "tarantool-${PACKAGE_VERSION}-src")

#
# Provide options for the binary distribution.
#
string (TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" platform)

set (CPACK_PACKAGE_FILE_NAME "tarantool-${PACKAGE_VERSION}-${platform}")
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-debug")
endif()


if (${TARGET_OS_DARWIN})
    set (CPACK_GENERATOR "PackageMaker")
    set (CPACK_SOURCE_GENERATOR "PackageMaker")
    set (CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
    set (CPACK_RESOURCE_FILE_WELCOME "${PROJECT_SOURCE_DIR}/extra/dmg/DESCRIPTION.html")
    set (CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/extra/dmg/LICENSE.html")
endif()

##
include (CPack)
