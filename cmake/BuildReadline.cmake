set(NCURSES_VERSION 6.3-20220716)
set(NCURSES_HASH 2b7a0e31ebbd8144680f985d61f5bbd5)
set(NCURSES_INSTALL_DIR ${BUNDLED_LIBS_INSTALL_DIR}/ncurses-prefix)
set(NCURSES_LIBRARY ${NCURSES_INSTALL_DIR}/lib/libncurses.a)
set(NCURSES_TINFO_LIBRARY ${NCURSES_INSTALL_DIR}/lib/libtinfo.a)
set(NCURSES_CFLAGS "${DEPENDENCY_CFLAGS} -O2")
set(NCURSES_CXXFLAGS "${DEPENDENCY_CXXFLAGS} -O2")
set(NCURSES_CPPFLAGS "")
set(NCURSES_LDFLAGS "")

set(READLINE_VERSION 8.0)
set(READLINE_HASH 7e6c1f16aee3244a69aba6e438295ca3)
set(READLINE_PATCHES_DIR ${PROJECT_SOURCE_DIR}/patches)
set(READLINE_INSTALL_DIR ${BUNDLED_LIBS_INSTALL_DIR}/readline-prefix)
set(READLINE_INCLUDE_DIR ${READLINE_INSTALL_DIR}/include)
set(READLINE_LIBRARY ${READLINE_INSTALL_DIR}/lib/libreadline.a)
set(READLINE_CFLAGS "${DEPENDENCY_CFLAGS} -O2")
set(READLINE_CPPFLAGS "")
set(READLINE_LDFLAGS "")

if(APPLE)
    set(NCURSES_CFLAGS "${NCURSES_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(NCURSES_CXXFLAGS "${NCURSES_CXXFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(NCURSES_CPPFLAGS "${NCURSES_CPPFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")

    set(READLINE_CFLAGS "${READLINE_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(READLINE_CPPFLAGS "${READLINE_CPPFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

ExternalProject_Add(bundled-ncurses-project
    PREFIX ${NCURSES_INSTALL_DIR}
    SOURCE_DIR ${NCURSES_INSTALL_DIR}/src/ncurses
    BINARY_DIR ${NCURSES_INSTALL_DIR}/src/ncurses-build
    STAMP_DIR ${NCURSES_INSTALL_DIR}/src/ncurses-stamp
    URL ${BACKUP_STORAGE}/ncurses/ncurses-${NCURSES_VERSION}.tgz
    URL_MD5 ${NCURSES_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${NCURSES_CFLAGS}
        CXXFLAGS=${NCURSES_CXXFLAGS}
        CPPFLAGS=${NCURSES_CPPFLAGS}
        LDFLAGS=${NCURSES_LDFLAGS}

        --prefix=<INSTALL_DIR>

        # This flag enables creation of libcurses.a as a symlink to libncurses.a
        # and disables subdir creation `ncurses` at <install_dir>/include. It is
        # necessary for correct work of FindCurses.cmake module (this module is
        # builtin at cmake package) which used in cmake/FindReadline.cmake
        --enable-overwrite

        # enable building libtinfo to prevent linking with libtinfo from system
        # directories
        --with-termlib

        # set search paths for terminfo db
        --with-terminfo-dirs=/lib/terminfo:/usr/share/terminfo:/etc/terminfo

        # disable install created terminfo db, use db from system
        --disable-db-install
        --without-progs
        --without-manpages
    BUILD_BYPRODUCTS ${NCURSES_LIBRARY} ${NCURSES_TINFO_LIBRARY}
)

add_library(bundled-ncurses STATIC IMPORTED GLOBAL)
set_target_properties(bundled-ncurses PROPERTIES IMPORTED_LOCATION
    ${NCURSES_LIBRARY})
add_dependencies(bundled-ncurses bundled-ncurses-project)

add_library(bundled-ncurses-tinfo STATIC IMPORTED GLOBAL)
set_target_properties(bundled-ncurses-tinfo PROPERTIES IMPORTED_LOCATION
    ${NCURSES_TINFO_LIBRARY})
add_dependencies(bundled-ncurses-tinfo bundled-ncurses-project)

ExternalProject_Add(bundled-readline-project
    PREFIX ${READLINE_INSTALL_DIR}
    SOURCE_DIR ${READLINE_INSTALL_DIR}/src/readline
    BINARY_DIR ${READLINE_INSTALL_DIR}/src/readline-build
    STAMP_DIR ${READLINE_INSTALL_DIR}/src/readline-stamp
    URL ${BACKUP_STORAGE}/readline/readline-${READLINE_VERSION}.tar.gz
    URL_MD5 ${READLINE_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${READLINE_CFLAGS}
        CPPFLAGS=${READLINE_CPPFLAGS}
        LDFLAGS=${READLINE_LDFLAGS}

        --prefix=<INSTALL_DIR>
        --disable-shared
    PATCH_COMMAND patch -d <SOURCE_DIR> -p0 -i "${READLINE_PATCHES_DIR}/readline80-001.patch"
    COMMAND       patch -d <SOURCE_DIR> -p1 -i "${READLINE_PATCHES_DIR}/readline-tarantool-security-95.patch"
    BUILD_BYPRODUCTS ${READLINE_LIBRARY}
)

add_library(bundled-readline STATIC IMPORTED GLOBAL)
set_target_properties(bundled-readline PROPERTIES IMPORTED_LOCATION
    ${READLINE_LIBRARY})
add_dependencies(bundled-readline bundled-readline-project
    bundled-ncurses bundled-ncurses-tinfo)

set(READLINE_FOUND TRUE)
set(READLINE_LIBRARIES ${READLINE_LIBRARY} ${NCURSES_LIBRARY}
    ${NCURSES_TINFO_LIBRARY})
set(READLINE_INCLUDE_DIRS ${READLINE_INCLUDE_DIR})

set(HAVE_GNU_READLINE TRUE)

message(STATUS "Using bundled readline")
