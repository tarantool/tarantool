set(ZLIB_VERSION 1.2.12)
set(ZLIB_HASH 5fc414a9726be31427b440b434d05f78)
set(ZLIB_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/zlib)
set(ZLIB_INCLUDE_DIR ${ZLIB_INSTALL_DIR}/include)
set(ZLIB_LIBRARY ${ZLIB_INSTALL_DIR}/lib/libz.a)
set(ZLIB_CFLAGS "${DEPENDENCY_CFLAGS} -O2")
set(ZLIB_CPPFLAGS "")
set(ZLIB_LDFLAGS "")

if(APPLE)
    set(ZLIB_CFLAGS "${ZLIB_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(ZLIB_CPPFLAGS "${ZLIB_CPPFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

ExternalProject_Add(bundled-zlib-project
    PREFIX ${ZLIB_INSTALL_DIR}
    URL ${BACKUP_STORAGE}/zlib/zlib-${ZLIB_VERSION}.tar.gz
    URL_MD5 ${ZLIB_HASH}
    CONFIGURE_COMMAND env
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${ZLIB_CFLAGS}
        CPPFLAGS=${ZLIB_CPPFLAGS}
        LDFLAGS=${ZLIB_LDFLAGS}
        <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --static
    BUILD_BYPRODUCTS ${ZLIB_LIBRARY}
)

add_library(bundled-zlib STATIC IMPORTED GLOBAL)
set_target_properties(bundled-zlib PROPERTIES IMPORTED_LOCATION
    ${ZLIB_LIBRARY})
add_dependencies(bundled-zlib bundled-zlib-project)

set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})

message(STATUS "Using bundled zlib")
