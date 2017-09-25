find_path(ZSTD_INCLUDE_DIR
  NAMES zstd.h
)

find_library(ZSTD_LIBRARY
  NAMES zstd
)

set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}")
set(ZSTD_LIBRARIES "${ZSTD_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZSTD REQUIRED_VARS
    ZSTD_LIBRARIES ZSTD_INCLUDE_DIRS)

mark_as_advanced(ZSTD_LIBRARY ZSTD_LIBRARIES
    ZSTD_INCLUDE_DIR ZSTD_INCLUDE_DIRS)
