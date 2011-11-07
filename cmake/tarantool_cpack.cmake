#
# List generators
#
set (CPACK_GENERATOR "TGZ")
set (CPACK_SOURCE_GENERATOR "TGZ")
#
# Describe the source distribution
#
set (CPACK_SOURCE_IGNORE_FILES "\\\\.git" "_CPack_Packages"
    "CMakeCache.txt" "CPackSourceConfig.cmake" "CPackConfig.cmake"
    "CMakeFiles" "\\\\.gz" "\\\\.Z" "\\\\.zip"
    "mod/box/tarantool_box"
    "mod/box/tarantool_feeder" "install_manifest.txt"
    "Makefile" "cmake_install.cmake" "test/var/" "\\\\.a")

set (CPACK_SOURCE_PACKAGE_FILE_NAME "tarantool-${TARANTOOL_VERSION}-src")
#
# Provide options for the binary distribution.
#
STRING(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" platform)

set (CPACK_PACKAGE_FILE_NAME "tarantool-${TARANTOOL_VERSION}-${platform}")
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set (CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}-debug")
endif()

#
#
include (CPack)
