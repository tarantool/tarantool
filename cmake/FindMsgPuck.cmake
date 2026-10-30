# - Find libmsgpuck header-only library
# The module defines the following variables:
#
#  MSGPUCK_FOUND - true if MsgPuck was found
#  MSGPUCK_INCLUDE_DIRS - the directory of the MsgPuck headers
#  MSGPUCK_LIBRARIES - the MsgPuck static library needed for linking
#

find_path(MSGPUCK_INCLUDE_DIR msgpuck.h PATH_SUFFIXES msgpuck)
find_library(MSGPUCK_LIBRARY NAMES libmsgpuck.a)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MsgPuck
    REQUIRED_VARS MSGPUCK_INCLUDE_DIR MSGPUCK_LIBRARY)
set(MSGPUCK_INCLUDE_DIRS ${MSGPUCK_INCLUDE_DIR})
set(MSGPUCK_LIBRARIES ${MSGPUCK_LIBRARY})
mark_as_advanced(MSGPUCK_INCLUDE_DIR MSGPUCK_INCLUDE_DIRS
                 MSGPUCK_LIBRARY MSGPUCK_LIBRARIES)
