cmake_minimum_required(VERSION 3.1)

# Detect system compilers for further dependencies configuring to be
# built with these compilers. This is used to build tarantool and
# it's dependencies by using one compiler system (for example libicu
# by default uses clang if it exists when others uses gcc/g++ on
# linux machine).
project(tarantool-static C CXX)

include(CheckLibraryExists)
include(CheckCSourceCompiles)
include(FindPackageMessage)
include(ExternalProject)
set(LIBICU_VERSION release-71-1/icu4c-71_1)
set(LIBICU_HASH e06ffc96f59762bd3c929b217445aaec)
set(LIBICONV_VERSION 1.17)
set(LIBICONV_HASH d718cd5a59438be666d1575855be72c3)
set(OPENSSL_VERSION 1.1.1q)
set(OPENSSL_HASH c685d239b6a6e1bd78be45624c092f51)
set(ZLIB_VERSION 1.2.12)
set(ZLIB_HASH 5fc414a9726be31427b440b434d05f78)
set(NCURSES_VERSION 6.3-20220716)
set(NCURSES_HASH 2b7a0e31ebbd8144680f985d61f5bbd5)
set(READLINE_VERSION 8.0)
set(READLINE_HASH 7e6c1f16aee3244a69aba6e438295ca3)
set(BACKUP_STORAGE https://distrib.hb.bizmrg.com)

include(../cmake/os.cmake)
include(../cmake/profile.cmake)
include(../cmake/hardening.cmake)
include(../cmake/prefix.cmake)

# Pass -isysroot=<SDK_PATH> option on Mac OS to a preprocessor and a C
# compiler to find header files installed with an SDK.
#
# The idea is to set these (DEPENDENCY_*) variables to corresponding
# environment variables at each depenency configure script.
#
# Note: Passing of CPPFLAGS / CFLAGS explicitly discards using of
# corresponsing environment variables. So pass empty LDFLAGS to discard
# using of corresponding environment variable. It is possible that a
# linker flag assumes that some compilation flag is set. We don't pass
# CFLAGS from environment, so we should not do it for LDFLAGS too.
set(DEPENDENCY_CFLAGS "")
set(DEPENDENCY_CXXFLAGS "")
set(DEPENDENCY_CPPFLAGS "")
set(DEPENDENCY_LDFLAGS)
if (APPLE)
    set(DEPENDENCY_CFLAGS   "${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(DEPENDENCY_CXXFLAGS "${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(DEPENDENCY_CPPFLAGS "${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

set(DEPENDENCY_CFLAGS "${DEPENDENCY_CFLAGS} ${HARDENING_FLAGS}")
set(DEPENDENCY_CXXFLAGS "${DEPENDENCY_CXXFLAGS} ${HARDENING_FLAGS}")
set(DEPENDENCY_LDFLAGS "${DEPENDENCY_LDFLAGS} ${HARDENING_LDFLAGS}")

set(DEPENDENCY_CFLAGS "${DEPENDENCY_CFLAGS} ${PREFIX_MAP_FLAGS}")
set(DEPENDENCY_CXXFLAGS "${DEPENDENCY_CXXFLAGS} ${PREFIX_MAP_FLAGS}")

# Install all libraries required by tarantool at current build dir

#
# OpenSSL
#
# Patched to build on Mac OS. See
# https://github.com/openssl/openssl/issues/18720
#
ExternalProject_Add(openssl
    URL ${BACKUP_STORAGE}/openssl/openssl-${OPENSSL_VERSION}.tar.gz
    URL_MD5 ${OPENSSL_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/config
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${DEPENDENCY_CFLAGS}
        CPPFLAGS=${DEPENDENCY_CPPFLAGS}
        LDFLAGS=${DEPENDENCY_LDFLAGS}

        --prefix=<INSTALL_DIR>
        --libdir=lib
        no-shared
    INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} install_sw
    PATCH_COMMAND patch -d <SOURCE_DIR> -p1 <
        "${CMAKE_CURRENT_SOURCE_DIR}/openssl-111q-gh-18720.patch"
)

#
# ICU
#
ExternalProject_Add(icu
    URL https://github.com/unicode-org/icu/releases/download/${LIBICU_VERSION}-src.tgz
    URL_MD5 ${LIBICU_HASH}
    # By default libicu is built by using clang/clang++ compiler (if it
    # exists). Here is a link for detecting compilers at libicu configure
    # script: https://github.com/unicode-org/icu/blob/7c7b8bd5702310b972f888299169bc3cc88bf0a6/icu4c/source/configure.ac#L135
    # This will cause the problem on linux machine: tarantool is built
    # with gcc/g++ and libicu is built with clang/clang++ (if it exists)
    # so at linking stage `rellocation` errors will occur. To solve this,
    # we can set CC/CXX to CMAKE_C_COMPILER/CMAKE_CXX_COMPILER variables
    # manually which are detected above (by cmake `project()` command)
    CONFIGURE_COMMAND <SOURCE_DIR>/source/configure
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${DEPENDENCY_CFLAGS}
        CXXFLAGS=${DEPENDENCY_CXXFLAGS}
        CPPFLAGS=${DEPENDENCY_CPPFLAGS}
        LDFLAGS=${DEPENDENCY_LDFLAGS}

        --with-data-packaging=static
        --prefix=<INSTALL_DIR>
        --disable-shared
        --enable-static
)

#
# ZLIB
#
ExternalProject_Add(zlib
    URL ${BACKUP_STORAGE}/zlib/zlib-${ZLIB_VERSION}.tar.gz
    URL_MD5 ${ZLIB_HASH}
    CONFIGURE_COMMAND env
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${DEPENDENCY_CFLAGS}
        CPPFLAGS=${DEPENDENCY_CPPFLAGS}
        LDFLAGS=${DEPENDENCY_LDFLAGS}
        <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --static
)

#
# Ncurses
#
ExternalProject_Add(ncurses
    URL ${BACKUP_STORAGE}/ncurses/ncurses-${NCURSES_VERSION}.tgz
    URL_MD5 ${NCURSES_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${DEPENDENCY_CFLAGS}
        CPPFLAGS=${DEPENDENCY_CPPFLAGS}
        CXXFLAGS=${DEPENDENCY_CXXFLAGS}
        LDFLAGS=${DEPENDENCY_LDFLAGS}

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
)

#
# ReadLine
#
# Patched to fix file descriptor leak with zero-length history file.
#
ExternalProject_Add(readline
    URL https://ftp.gnu.org/gnu/readline/readline-${READLINE_VERSION}.tar.gz
    URL_MD5 ${READLINE_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        CC=${CMAKE_C_COMPILER}
        CFLAGS=${DEPENDENCY_CFLAGS}
        CPPFLAGS=${DEPENDENCY_CPPFLAGS}
        LDFLAGS=${DEPENDENCY_LDFLAGS}

        --prefix=<INSTALL_DIR>
        --disable-shared
    PATCH_COMMAND patch -d <SOURCE_DIR> -p0 <
        "${CMAKE_CURRENT_SOURCE_DIR}/readline80-001.patch"
)

#
# ICONV
#
if (APPLE)
    ExternalProject_Add(iconv
        URL https://ftp.gnu.org/pub/gnu/libiconv/libiconv-${LIBICONV_VERSION}.tar.gz
        URL_MD5 ${LIBICONV_HASH}
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            CC=${CMAKE_C_COMPILER}
            CFLAGS=${DEPENDENCY_CFLAGS}
            CPPFLAGS=${DEPENDENCY_CPPFLAGS}
            LDFLAGS=${DEPENDENCY_LDFLAGS}

            --prefix=<INSTALL_DIR>
            --disable-shared
            --enable-static
            --with-gnu-ld
        STEP_TARGETS download
    )
else()
    # In linux iconv is embedded into glibc
    # So we find system header and copy it locally
    find_path(ICONV_INCLUDE_DIR iconv.h)
    if(NOT ICONV_INCLUDE_DIR)
        message(FATAL_ERROR "iconv include header not found")
    endif()

    set(ICONV_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/iconv-prefix")

    add_custom_command(
        OUTPUT "${ICONV_INSTALL_PREFIX}/include/iconv.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${ICONV_INSTALL_PREFIX}/include"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${ICONV_INCLUDE_DIR}/iconv.h"
            "${ICONV_INSTALL_PREFIX}/include/iconv.h"
    )
    add_custom_target(iconv
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/iconv-prefix/include/iconv.h"
    )
    # This is a hack for further getting install directory of library
    # by ExternalProject_Get_Property
    set_target_properties(iconv
        PROPERTIES _EP_INSTALL_DIR ${ICONV_INSTALL_PREFIX}
    )
endif()

# Get install directories of builded libraries for building
# tarantool with custon CMAKE_PREFIX_PATH
foreach(PROJ openssl icu zlib ncurses readline iconv)
    ExternalProject_Get_Property(${PROJ} install_dir)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH}:${install_dir})
    set(TARANTOOL_DEPENDS ${PROJ} ${TARANTOOL_DEPENDS})
    message(STATUS "Add external project ${PROJ} in ${install_dir}")
endforeach()

ExternalProject_Add(tarantool
    DEPENDS ${TARANTOOL_DEPENDS}
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..
    LIST_SEPARATOR :
    CMAKE_ARGS
        # Override LOCALSTATEDIR to avoid cmake "special" cases:
        # https://cmake.org/cmake/help/v3.4/module/GNUInstallDirs.html#special-cases
        -DCMAKE_INSTALL_LOCALSTATEDIR=<INSTALL_DIR>/var
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DBUILD_STATIC=TRUE
        -DENABLE_DIST=TRUE
        -DENABLE_BACKTRACE=TRUE
        -DENABLE_HARDENING=${ENABLE_HARDENING}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        ${CMAKE_TARANTOOL_ARGS}
    STEP_TARGETS build
    BUILD_COMMAND $(MAKE)
    BUILD_ALWAYS TRUE
)

enable_testing()
ExternalProject_Get_Property(tarantool install_dir)
SET(TARANTOOL_BINARY ${install_dir}/bin/tarantool)

add_test(
    NAME check-dependencies
    COMMAND ${CMAKE_COMMAND}
        -D FILE=${TARANTOOL_BINARY}
        -P CheckDependencies.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test
)

add_test(
    NAME check-exports
    COMMAND ${TARANTOOL_BINARY}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/static-build/exports.test.lua
)

add_test(
    NAME check-traceback
    COMMAND ${TARANTOOL_BINARY}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/static-build/traceback.test.lua
)

add_test(
    NAME check-luarocks
    COMMAND ${TARANTOOL_BINARY}
            ${CMAKE_CURRENT_SOURCE_DIR}/test/static-build/luarocks.test.lua
)
