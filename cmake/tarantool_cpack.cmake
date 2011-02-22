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
    "mod/silverbox/tarantool_silverbox"
    "mod/silverbox/tarantool_feeder" "install_manifest.txt"
    "Makefile" "cmake_install.cmake" "test/var/" "\\\\.a")
set (CPACK_SOURCE_PACKAGE_FILE_NAME
"tarantool-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-src")
#
# Provide options for the binary distribution.
#
STRING(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" platform)

set (CPACK_PACKAGE_FILE_NAME
"tarantool-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-${platform}")

#
#
include (CPack)
