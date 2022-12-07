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

include(ext_project_autotools)

set(LIBUNWIND_CFLAGS "${DEPENDENCY_CFLAGS} -g -O2")
set(LIBUNWIND_CXXFLAGS "-g -O2")

ext_project_autotools(libunwind-build
    DIR
        third_party/libunwind
    CONFIGURE
        AR=${CMAKE_AR}
        CC=${CMAKE_C_COMPILER}
        CXX=${CMAKE_CXX_COMPILER}
        CFLAGS=${LIBUNWIND_CFLAGS}
        CXXFLAGS=${LIBUNWIND_CXXFLAGS}
        # Bundled libraries are linked statically.
        --disable-shared
        # Ditto.
        --enable-static
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L122-L125.
        --disable-coredump
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L130-L133.
        --disable-ptrace
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L138-L141.
        --disable-setjmp
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L143-L145
        --disable-documentation
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L147-L149
        --disable-tests
        # By default libunwind provides a weak alias to
        # `backtrace` function: this can lead to a conflict with
        # glibc's `backtrace`, see https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L151-L153
        --disable-weak-backtrace
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L155-L157
        --disable-unwind-header
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L302-L317
        --disable-minidebuginfo
        # See https://github.com/libunwind/libunwind/blob/e07b43c02d5cf1ea060c018fdf2e2ad34b7c7d80/configure.ac#L319-L334
        --disable-zlibdebuginfo
    BYPRODUCTS
        src/.libs/libunwind.a
        src/.libs/libunwind-${CMAKE_SYSTEM_PROCESSOR}.a
)
unset(LIBUNWIND_CFLAGS)
unset(LIBUNWIND_CXXFLAGS)

set(LIBUNWIND_BUILD_DIR ${CMAKE_BINARY_DIR}/third_party/libunwind)

add_library(bundled-libunwind STATIC IMPORTED GLOBAL)
set_target_properties(bundled-libunwind PROPERTIES
          IMPORTED_LOCATION
          ${LIBUNWIND_BUILD_DIR}/src/.lib/libunwind.a)
add_dependencies(bundled-libunwind libunwind-build)

add_library(bundled-libunwind-platform STATIC IMPORTED GLOBAL)
set_target_properties(bundled-libunwind-platform PROPERTIES
          IMPORTED_LOCATION
          ${LIBUNWIND_BUILD_DIR}/src/.lib/libunwind-${CMAKE_SYSTEM_PROCESSOR}.a)
add_dependencies(bundled-libunwind-platform libunwind-build)

set(LIBUNWIND_INCLUDE_DIR
    ${LIBUNWIND_BUILD_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/libunwind/include)
set(LIBUNWIND_LIBRARIES
    ${LIBUNWIND_BUILD_DIR}/src/.libs/libunwind-${CMAKE_SYSTEM_PROCESSOR}.a
    ${LIBUNWIND_BUILD_DIR}/src/.libs/libunwind.a)

unset(LIBUNWIND_BUILD_DIR)
message(STATUS "Using bundled libunwind")
