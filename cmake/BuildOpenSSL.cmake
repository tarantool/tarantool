set(OPENSSL_VERSION 1.1.1q)
set(OPENSSL_HASH c685d239b6a6e1bd78be45624c092f51)
set(OPENSSL_PATCHES_DIR ${PROJECT_SOURCE_DIR}/patches)
set(OPENSSL_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/openssl)
set(OPENSSL_INCLUDE_DIR ${OPENSSL_INSTALL_DIR}/include)
set(OPENSSL_CRYPTO_LIBRARY ${OPENSSL_INSTALL_DIR}/lib/libcrypto.a)
set(OPENSSL_SSL_LIBRARY ${OPENSSL_INSTALL_DIR}/lib/libssl.a)
set(OPENSSL_CFLAGS "${DEPENDENCY_CFLAGS} -O2")
set(OPENSSL_CPPFLAGS "")
set(OPENSSL_LDFLAGS "")

if(APPLE)
    set(OPENSSL_CFLAGS "${OPENSSL_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(OPENSSL_CPPFLAGS "${OPENSSL_CPPFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

ExternalProject_Add(bundled-openssl-project
    PREFIX ${OPENSSL_INSTALL_DIR}
    URL ${BACKUP_STORAGE}/openssl/openssl-${OPENSSL_VERSION}.tar.gz
    URL_MD5 ${OPENSSL_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/config
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${OPENSSL_CFLAGS}
        CPPFLAGS=${OPENSSL_CPPFLAGS}
        LDFLAGS=${OPENSSL_LDFLAGS}

        --prefix=<INSTALL_DIR>
        --libdir=lib
        no-shared
    INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} install_sw
    PATCH_COMMAND patch -d <SOURCE_DIR> -p1 -i "${OPENSSL_PATCHES_DIR}/openssl-111q-gh-18720.patch"
    COMMAND       patch -d <SOURCE_DIR> -p1 -i "${OPENSSL_PATCHES_DIR}/openssl-tarantool-security-27.patch"
    COMMAND       patch -d <SOURCE_DIR> -p1 -i "${OPENSSL_PATCHES_DIR}/openssl-tarantool-security-54.patch"
    COMMAND       patch -d <SOURCE_DIR> -p1 -i "${OPENSSL_PATCHES_DIR}/openssl-tarantool-security-90.patch"
    BUILD_BYPRODUCTS ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY}
)

add_library(bundled-openssl-ssl STATIC IMPORTED GLOBAL)
set_target_properties(bundled-openssl-ssl PROPERTIES IMPORTED_LOCATION
    ${OPENSSL_SSL_LIBRARY})
add_dependencies(bundled-openssl-ssl bundled-openssl-project)

add_library(bundled-openssl-crypto STATIC IMPORTED GLOBAL)
set_target_properties(bundled-openssl-crypto PROPERTIES IMPORTED_LOCATION
    ${OPENSSL_CRYPTO_LIBRARY})
add_dependencies(bundled-openssl-crypto bundled-openssl-project)

add_custom_target(bundled-openssl
    DEPENDS bundled-openssl-ssl bundled-openssl-crypto)

set(OPENSSL_FOUND TRUE)
set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}
    ${CMAKE_DL_LIBS})
set(OPENSSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})

message(STATUS "Using bundled openssl")
