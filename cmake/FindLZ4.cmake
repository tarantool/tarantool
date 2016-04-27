# - Find liblz4 library
# The module defines the following variables:
#
#  LZ4_FOUND - true if MsgPuck was found
#  LZ4_INCLUDE_DIRS - the directory of the MsgPuck headers
#  LZ4_LIBRARIES - the MsgPuck static library needed for linking
#

find_path(LZ4_INCLUDE_DIR lz4.h lz4frame.h lz4hc.h PATH_SUFFIXES lz4)
find_library(LZ4_LIBRARY NAMES lz4)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LZ4
    REQUIRED_VARS LZ4_INCLUDE_DIR LZ4_LIBRARY)
set(LZ4_INCLUDE_DIRS ${LZ4_INCLUDE_DIR})
set(LZ4_LIBRARIES ${LZ4_LIBRARY})
mark_as_advanced(LZ4_INCLUDE_DIR LZ4_INCLUDE_DIRS
                 LZ4_LIBRARY LZ4_LIBRARIES)
