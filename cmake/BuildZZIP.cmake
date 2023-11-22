set(ZZIP_VERSION v0.13.71)
set(ZZIP_HASH 1aa094186cf2222e4cda1b91b8fb8f60)
set(ZZIP_INSTALL_DIR ${BUNDLED_LIBS_INSTALL_DIR}/zzip-prefix)
set(ZZIP_INCLUDE_DIR ${ZZIP_INSTALL_DIR}/include)
set(ZZIP_LIBRARY ${ZZIP_INSTALL_DIR}/lib/libzzip-0.a)
set(ZZIP_CFLAGS "${DEPENDENCY_CFLAGS} -O2")

if(APPLE)
    set(ZZIP_CFLAGS "${ZZIP_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

set(ZZIP_CMAKE_FLAGS
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_C_FLAGS=${ZZIP_CFLAGS}"
    "-DCMAKE_INSTALL_PREFIX=${ZZIP_INSTALL_DIR}"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_STATIC_LIBS=TRUE"
    "-DBUILD_SHARED_LIBS=FALSE"
    "-DZZIPDOCS=FALSE"
    "-DZZIPBINS=FALSE"
    "-DZZIPWRAP=FALSE"
    "-DZZIPTEST=FALSE"
    "-DZZIPSDL=FALSE"
)

if(ENABLED_BUNDLED_ZLIB)
    list(APPEND ZZIP_CMAKE_FLAGS
        "-DCMAKE_PREFIX_PATH=${ZLIB_INSTALL_DIR}"
        "-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE"
    )
endif()

ExternalProject_Add(bundled-zzip-project
    PREFIX ${ZZIP_INSTALL_DIR}
    SOURCE_DIR ${ZZIP_INSTALL_DIR}/src/zzip
    BINARY_DIR ${ZZIP_INSTALL_DIR}/src/zzip-build
    STAMP_DIR ${ZZIP_INSTALL_DIR}/src/zzip-stamp
    URL https://github.com/gdraheim/zziplib/archive/${ZZIP_VERSION}.tar.gz
    URL_MD5 ${ZZIP_HASH}
    CMAKE_ARGS ${ZZIP_CMAKE_FLAGS}
    BUILD_BYPRODUCTS ${ZZIP_LIBRARY}
)

if(ENABLE_BUNDLED_ZLIB)
    add_dependencies(bundled-zzip-project bundled-zlib)
endif()

add_library(bundled-zzip STATIC IMPORTED GLOBAL)
set_target_properties(bundled-zzip PROPERTIES IMPORTED_LOCATION
    ${ZZIP_LIBRARY})
add_dependencies(bundled-zzip bundled-zzip-project)

set(ZZIP_FOUND TRUE)
set(ZZIP_LIBRARIES ${ZZIP_LIBRARY})
set(ZZIP_INCLUDE_DIRS ${ZZIP_INCLUDE_DIR})

message(STATUS "Using bundled zzip")
