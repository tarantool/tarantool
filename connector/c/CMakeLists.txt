
#
# library soname version
#
set(LIBTNT_VERSION_MAJOR "1")
set(LIBTNT_VERSION_MINOR "1")

set(LIBTNT_VERSION   "${LIBTNT_VERSION_MAJOR}.${LIBTNT_VERSION_MINOR}")
set(LIBTNT_SOVERSION "${LIBTNT_VERSION_MAJOR}")

add_subdirectory(tnt)
add_subdirectory(tntsql)
add_subdirectory(tntnet)
add_subdirectory(include)
