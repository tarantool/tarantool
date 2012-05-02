
#
# library soname version
#
set(LIBTNT_VERSION   "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(LIBTNT_SOVERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")

add_subdirectory(tnt)
add_subdirectory(tntsql)
add_subdirectory(tntnet)
add_subdirectory(include)
