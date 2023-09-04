#[========================================================================[.rst:
FindLibUnwind
--------
Finds the libunwind library.

Result Variables
^^^^^^^^^^^^^^^^
``LIBUNWIND_FOUND``
  True if the system has the libunwind library.
``LIBUNWIND_VERSION``
  The version of the libunwind library which was found.
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

include(FindPackageHandleStandardArgs)
include(GetLibUnwindVersion)

find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBUNWIND QUIET libunwind)

find_path(LIBUNWIND_INCLUDE_DIR libunwind.h ${PC_LIBUNWIND_INCLUDE_DIRS})
if(LIBUNWIND_INCLUDE_DIR)
    include_directories(${LIBUNWIND_INCLUDE_DIR})
endif()

if(BUILD_STATIC AND NOT APPLE)
    set(LIBUNWIND_LIBRARY_NAME libunwind.a)
else()
    # Only a dynamic version of libunwind is available on macOS: also, we
    # should link against the umbrella framework `System` — otherwise `ld` will
    # complain that it cannot link directly with libunwind.tbd.
    set(LIBUNWIND_LIBRARY_NAME System unwind)
endif()
find_library(LIBUNWIND_LIBRARY NAMES ${LIBUNWIND_LIBRARY_NAME}
             PATHS ${PC_LIBUNWIND_LIBRARY_DIRS})

if(APPLE)
    set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARY})
else()
    if(BUILD_STATIC)
        set(LIBUNWIND_PLATFORM_LIBRARY_NAME
            "libunwind-${CMAKE_SYSTEM_PROCESSOR}.a")
    else()
        set(LIBUNWIND_PLATFORM_LIBRARY_NAME
            "unwind-${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    find_library(LIBUNWIND_PLATFORM_LIBRARY ${LIBUNWIND_PLATFORM_LIBRARY_NAME}
                 ${PC_LIBUNWIND_LIBRARY_DIRS})
    set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARY} ${LIBUNWIND_PLATFORM_LIBRARY})
endif()

if(BUILD_STATIC)
    # libunwind could have been built with liblzma dependency:
    # https://github.com/libunwind/libunwind/blob/4feb1152d1c4aaafbb2d504dbe34c6db5b6fe9f2/configure.ac#L302-L317
    pkg_check_modules(PC_LIBLZMA QUIET liblzma)
    find_library(LIBLZMA_LIBRARY liblzma.a ${PC_LIBLZMA_LIBRARY_DIRS})
    if(NOT LIBLZMA_LIBRARY STREQUAL "LIBLZMA_LIBRARY-NOTFOUND")
        message(STATUS "liblzma found")
        set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARIES} ${LIBLZMA_LIBRARY})
    endif()
    # Ditto,
    # https://github.com/libunwind/libunwind/blob/4feb1152d1c4aaafbb2d504dbe34c6db5b6fe9f2/configure.ac#L319-L334
    set(LIBUNWIND_LIBRARIES ${LIBUNWIND_LIBRARIES} ZLIB::ZLIB)
endif()

if(PC_LIBUNWIND_VERSION)
    set(LIBUNWIND_VERSION ${PC_LIBUNWIND_VERSION})
else()
    GetLibUnwindVersion(LIBUNWIND_VERSION)
endif()

find_package_handle_standard_args(LibUnwind
      VERSION_VAR LIBUNWIND_VERSION
      REQUIRED_VARS LIBUNWIND_INCLUDE_DIR LIBUNWIND_LIBRARIES)

mark_as_advanced(LIBUNWIND_INCLUDE_DIR LIBUNWIND_LIBRARIES)
