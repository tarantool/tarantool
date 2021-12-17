find_path(ZZIP_INCLUDE_DIR
  NAMES zzip.h
)

if(BUILD_STATIC)
    set(ZZIP_LIB_NAMES libzzip.a libzzip-0.a)
else()
    set(ZZIP_LIB_NAMES zzip zzip-0)
endif()

find_library(ZZIP_LIBRARY
    NAMES ${ZZIP_LIB_NAMES}
)

set(ZZIP_INCLUDE_DIRS "${ZZIP_INCLUDE_DIR}")
set(ZZIP_LIBRARIES "${ZZIP_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZZIP REQUIRED_VARS
    ZZIP_LIBRARY ZZIP_INCLUDE_DIR)

mark_as_advanced(ZZIP_LIBRARY ZZIP_INCLUDE_DIR)
