find_path(ZSTD_INCLUDE_DIR
  NAMES zstd.h
)

if(BUILD_STATIC)
    set(ZSTD_LIB_NAME libzstd.a)
else()
    set(ZSTD_LIB_NAME zstd)
endif()
find_library(ZSTD_LIBRARY
    NAMES ${ZSTD_LIB_NAME}
)

set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}")
set(ZSTD_LIBRARIES "${ZSTD_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZSTD REQUIRED_VARS
    ZSTD_LIBRARIES ZSTD_INCLUDE_DIRS)

mark_as_advanced(ZSTD_LIBRARY ZSTD_LIBRARIES
    ZSTD_INCLUDE_DIR ZSTD_INCLUDE_DIRS)
