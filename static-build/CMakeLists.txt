cmake_minimum_required(VERSION 2.8)

# Detect system compilers for further dependencies configuring to be
# built with these compilers. This is used to build tarantool and
# it's dependencies by using one compiler system (for example libicu
# by default uses clang if it exists when others uses gcc/g++ on
# linux machine).
project(tarantool-static C CXX)

include(ExternalProject)
set(LIBICU_VERSION release-62-1/icu4c-62_1)
set(LIBICU_HASH 490ad9d920158e0314e10ba74ae9a150)
set(LIBICONV_VERSION 1.16)
set(LIBICONV_HASH 7d2a800b952942bb2880efb00cfd524c)
set(OPENSSL_VERSION 1.1.1f)
set(OPENSSL_HASH 3f486f2f4435ef14b81814dbbc7b48bb)
set(ZLIB_VERSION 1.2.11)
set(ZLIB_HASH 1c9f62f0778697a09d36121ead88e08e)
set(NCURSES_VERSION 6.2)
set(NCURSES_HASH e812da327b1c2214ac1aed440ea3ae8d)
set(READLINE_VERSION 8.0)
set(READLINE_HASH 7e6c1f16aee3244a69aba6e438295ca3)
set(UNWIND_VERSION 1.3-rc1)
set(UNWIND_HASH f09b670de5db6430a3de666e6aed60e3)

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
set(DEPENDENCY_CPPFLAGS "")
set(DEPENDENCY_LDFLAGS)
if (APPLE)
    set(DEPENDENCY_CFLAGS   "${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(DEPENDENCY_CPPFLAGS "${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

# Install all libraries required by tarantool at current build dir

#
# OpenSSL
#
ExternalProject_Add(openssl
    URL https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz
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
    URL https://zlib.net/zlib-${ZLIB_VERSION}.tar.gz
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
    URL https://ftp.gnu.org/gnu/ncurses/ncurses-${NCURSES_VERSION}.tar.gz
    URL_MD5 ${NCURSES_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${DEPENDENCY_CFLAGS}
        CPPFLAGS=${DEPENDENCY_CPPFLAGS}
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

#
# Unwind
#
if (APPLE)
    # On macOS libunwind is a part of MacOSX.sdk
    # So we need to find library and header and
    # copy it locally
    find_path(UNWIND_INCLUDE_DIR libunwind.h)
    find_library(UNWIND_LIBRARY libunwind.dylib
        PATH_SUFFIXES system
    )

    set(UNWIND_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/unwind-prefix")

    set(UNWIND_DEPENDENCIES)

    if (UNWIND_INCLUDE_DIR AND UNWIND_LIBRARY)
        add_custom_command(
            OUTPUT "${UNWIND_INSTALL_PREFIX}/include/unwind.h"
            OUTPUT "${UNWIND_INSTALL_PREFIX}/include/libunwind.h"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${UNWIND_INSTALL_PREFIX}/include"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${UNWIND_INCLUDE_DIR}/libunwind.h"
                "${UNWIND_INCLUDE_DIR}/unwind.h"
                "${UNWIND_INSTALL_PREFIX}/include/"
        )
        add_custom_command(
            OUTPUT "${UNWIND_INSTALL_PREFIX}/lib/libunwind.dylib"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${UNWIND_INSTALL_PREFIX}/lib"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${UNWIND_LIBRARY}"
                "${UNWIND_INSTALL_PREFIX}/lib/"
        )
        set(UNWIND_DEPENDENCIES
            ${UNWIND_DEPENDENCIES}
            "${UNWIND_INSTALL_PREFIX}/lib/libunwind.dylib"
            "${UNWIND_INSTALL_PREFIX}/include/libunwind.h"
        )
    else()
        message(STATUS "Unwind not found")
    endif()

    add_custom_target(unwind DEPENDS ${UNWIND_DEPENDENCIES})
    # This is a hack for further getting install directory of library
    # by ExternalProject_Get_Property
    set_target_properties(unwind
        PROPERTIES _EP_INSTALL_DIR ${UNWIND_INSTALL_PREFIX}
    )
else()
    ExternalProject_Add(unwind
        URL https://download.savannah.nongnu.org/releases/libunwind/libunwind-${UNWIND_VERSION}.tar.gz
        URL_MD5 ${UNWIND_HASH}
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            CC=${CMAKE_C_COMPILER}
            CXX=${CMAKE_CXX_COMPILER}
            CFLAGS=${DEPENDENCY_CFLAGS}
            CPPFLAGS=${DEPENDENCY_CPPFLAGS}
            LDFLAGS=${DEPENDENCY_LDFLAGS}
            --prefix=<INSTALL_DIR>
            --disable-shared
            --enable-static
            --disable-minidebuginfo # to prevent linking with liblzma
        STEP_TARGETS download
    )
endif()

# Get install directories of builded libraries for building
# tarantool with custon CMAKE_PREFIX_PATH
foreach(PROJ openssl icu zlib ncurses readline iconv unwind)
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
        -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DBUILD_STATIC=TRUE
        -DENABLE_DIST=TRUE
        -DENABLE_BACKTRACE=TRUE
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        ${CMAKE_TARANTOOL_ARGS}
    STEP_TARGETS build
    BUILD_COMMAND $(MAKE)
)

enable_testing()
ExternalProject_Get_Property(tarantool binary_dir)
SET(TARANTOOL_BINARY_DIR ${binary_dir})

add_test(
    NAME check-dependencies
    COMMAND ${CMAKE_COMMAND}
        -D FILE=${TARANTOOL_BINARY_DIR}/src/tarantool
        -P CheckDependencies.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test
)

add_test(
    NAME check-exports
    COMMAND ${TARANTOOL_BINARY_DIR}/src/tarantool
            ${CMAKE_CURRENT_SOURCE_DIR}/test/static-build/exports.test.lua
)

add_test(
    NAME check-traceback
    COMMAND ${TARANTOOL_BINARY_DIR}/src/tarantool
            ${CMAKE_CURRENT_SOURCE_DIR}/test/static-build/traceback.test.lua
)
