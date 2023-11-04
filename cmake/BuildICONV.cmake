set(ICONV_VERSION 1.17)
set(ICONV_HASH d718cd5a59438be666d1575855be72c3)
set(ICONV_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/iconv)
set(ICONV_INCLUDE_DIR ${ICONV_INSTALL_DIR}/include)
set(ICONV_LIBRARY ${ICONV_INSTALL_DIR}/lib/libiconv.a)
set(ICONV_CFLAGS "${DEPENDENCY_CFLAGS} -O2")
set(ICONV_CPPFLAGS "")
set(ICONV_LDFLAGS "")

if(APPLE)
    set(ICONV_CFLAGS "${ICONV_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(ICONV_CPPFLAGS "${ICONV_CPPFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

ExternalProject_Add(bundled-iconv-project
    PREFIX ${ICONV_INSTALL_DIR}
    URL URL ${BACKUP_STORAGE}/libiconv/libiconv-${ICONV_VERSION}.tar.gz
    URL_MD5 ${ICONV_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${ICONV_CFLAGS}
        CPPFLAGS=${ICONV_CPPFLAGS}
        LDFLAGS=${ICONV_LDFLAGS}

        --prefix=<INSTALL_DIR>
        --disable-shared
        --enable-static
        --with-gnu-ld
    STEP_TARGETS download
    BUILD_BYPRODUCTS ${ICONV_LIBRARY}
)

add_library(bundled-iconv STATIC IMPORTED GLOBAL)
set_target_properties(bundled-iconv PROPERTIES IMPORTED_LOCATION
    ${ICONV_LIBRARY})
add_dependencies(bundled-iconv bundled-iconv-project)

set(ICONV_FOUND TRUE)
set(ICONV_LIBRARIES ${ICONV_LIBRARY})
set(ICONV_INCLUDE_DIRS ${ICONV_INCLUDE_DIR})

message(STATUS "Using bundled iconv")
