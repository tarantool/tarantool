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

Cache Variables
^^^^^^^^^^^^^^^
``LIBUNWIND_INCLUDE_DIR``
  The directory containing ``libunwind.h``.
``LIBUNWIND_LIBRARIES``
  The paths to the libunwind libraries.
#]========================================================================]

macro(libunwind_build)
    set(LIBUNWIND_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/libunwind)
    set(LIBUNWIND_BUILD_DIR ${PROJECT_BINARY_DIR}/build/libunwind)
    set(LIBUNWIND_BINARY_DIR ${LIBUNWIND_BUILD_DIR}/work)
    set(LIBUNWIND_INSTALL_DIR ${LIBUNWIND_BUILD_DIR}/dest)

    include(ExternalProject)
    ExternalProject_Add(bundled-libunwind-project
                        TMP_DIR ${LIBUNWIND_BUILD_DIR}/tmp
                        STAMP_DIR ${LIBUNWIND_BUILD_DIR}/stamp
                        SOURCE_DIR ${LIBUNWIND_SOURCE_DIR}
                        BINARY_DIR ${LIBUNWIND_BINARY_DIR}
                        INSTALL_DIR ${LIBUNWIND_INSTALL_DIR}

                        DOWNLOAD_COMMAND ""

                        CONFIGURE_COMMAND
                        <SOURCE_DIR>/configure
                        CC=${CMAKE_C_COMPILER}
                        CXX=${CMAKE_CXX_COMPILER}
                        --prefix=<INSTALL_DIR>
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

                        LOG_CONFIGURE TRUE
                        LOG_BUILD TRUE
                        LOG_INSTALL TRUE
                        LOG_MERGED_STDOUTERR TRUE
                        LOG_OUTPUT_ON_FAILURE TRUE

                        EXCLUDE_FROM_ALL)

    add_library(bundled-libunwind STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-libunwind PROPERTIES
                          IMPORTED_LOCATION
                          ${LIBUNWIND_INSTALL_DIR}/lib/libunwind.a)
    add_dependencies(bundled-libunwind bundled-libunwind-project)

    add_library(bundled-libunwind-platform STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-libunwind-platform PROPERTIES
                          IMPORTED_LOCATION
                          ${LIBUNWIND_INSTALL_DIR}/lib/libunwind-${CMAKE_SYSTEM_PROCESSOR}.a)
    add_dependencies(bundled-libunwind-platform bundled-libunwind-project)

    set(LIBUNWIND_INCLUDE_DIR ${LIBUNWIND_INSTALL_DIR}/include)
    set(LIBUNWIND_LIBRARIES
        ${LIBUNWIND_INSTALL_DIR}/lib/libunwind-${CMAKE_SYSTEM_PROCESSOR}.a
        ${LIBUNWIND_INSTALL_DIR}/lib/libunwind.a)

    message(STATUS "Using bundled libunwind")

    unset(LIBUNWIND_SOURCE_DIR)
    unset(LIBUNWIND_BUILD_DIR)
    unset(LIBUNWIND_BINARY_DIR)
    unset(LIBUNWIND_INSTALL_DIR)
endmacro()
