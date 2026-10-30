#[========================================================================[.rst:
libunwind_build
--------
Builds the libunwind library.

Result Variables
^^^^^^^^^^^^^^^^
``LIBUNWIND_INCLUDE_DIR``
  Include directory needed to use libunwind.
``LIBUNWIND_LIBRARIES``
  Libraries needed to link to libunwind.
#]========================================================================]

set(LIBUNWIND_VERSION 1.8.3)
set(LIBUNWIND_HASH
    be30d910e67f58d82e753231f1357f326a1a088acf126b21ff77e60aab19b90b)
set(LIBUNWIND_INSTALL_DIR ${BUNDLED_LIBS_INSTALL_DIR}/libunwind-prefix)

set(LIBUNWIND_CFLAGS "${DEPENDENCY_CFLAGS} -g -O2")
set(LIBUNWIND_CXXFLAGS "-g -O2")

set(LIBUNWIND_LIBRARY ${LIBUNWIND_INSTALL_DIR}/lib/libunwind.a)
set(LIBUNWIND_PLATFORM_LIBRARY
    ${LIBUNWIND_INSTALL_DIR}/lib/libunwind-${CMAKE_SYSTEM_PROCESSOR}.a)

# The release tarball contains the generated configure script, so the
# autotools are not needed.
ExternalProject_Add(bundled-libunwind-project
    PREFIX ${LIBUNWIND_INSTALL_DIR}
    SOURCE_DIR ${LIBUNWIND_INSTALL_DIR}/src/libunwind
    BINARY_DIR ${LIBUNWIND_INSTALL_DIR}/src/libunwind-build
    STAMP_DIR ${LIBUNWIND_INSTALL_DIR}/src/libunwind-stamp
    URL ${BACKUP_STORAGE}/libunwind/libunwind-${LIBUNWIND_VERSION}.tar.gz
    URL_HASH SHA256=${LIBUNWIND_HASH}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        AR=${CMAKE_AR}
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${LIBUNWIND_CFLAGS}
        CXXFLAGS=${LIBUNWIND_CXXFLAGS}

        --prefix=<INSTALL_DIR>
        --libdir=<INSTALL_DIR>/lib

        # Bundled libraries are linked statically.
        --disable-shared
        --enable-static
        # Only libunwind itself is used, not its coredump, ptrace and setjmp
        # libraries.
        --disable-coredump
        --disable-ptrace
        --disable-setjmp
        --disable-documentation
        --disable-tests
        # By default libunwind provides a weak alias to `backtrace`
        # function: this can lead to a conflict with glibc's `backtrace`.
        --disable-weak-backtrace
        # Do not install unwind.h, it may conflict with the compiler one.
        --disable-unwind-header
        # Do not depend on liblzma and zlib.
        --disable-minidebuginfo
        --disable-zlibdebuginfo
    BUILD_BYPRODUCTS ${LIBUNWIND_LIBRARY} ${LIBUNWIND_PLATFORM_LIBRARY}
)

add_library(bundled-libunwind STATIC IMPORTED GLOBAL)
set_target_properties(bundled-libunwind PROPERTIES IMPORTED_LOCATION
    ${LIBUNWIND_LIBRARY})
add_dependencies(bundled-libunwind bundled-libunwind-project)

add_library(bundled-libunwind-platform STATIC IMPORTED GLOBAL)
set_target_properties(bundled-libunwind-platform PROPERTIES IMPORTED_LOCATION
    ${LIBUNWIND_PLATFORM_LIBRARY})
add_dependencies(bundled-libunwind-platform bundled-libunwind-project)

set(LIBUNWIND_INCLUDE_DIR ${LIBUNWIND_INSTALL_DIR}/include)
set(LIBUNWIND_LIBRARIES ${LIBUNWIND_PLATFORM_LIBRARY} ${LIBUNWIND_LIBRARY})

unset(LIBUNWIND_CFLAGS)
unset(LIBUNWIND_CXXFLAGS)
unset(LIBUNWIND_LIBRARY)
unset(LIBUNWIND_PLATFORM_LIBRARY)
unset(LIBUNWIND_INSTALL_DIR)
unset(LIBUNWIND_HASH)
message(STATUS "Using bundled libunwind ${LIBUNWIND_VERSION}")
