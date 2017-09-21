# - Find ICU header and library
# The module defines the following variables:
#
#  ICU_FOUND - true if ICU was found
#  ICU_INCLUDE_DIRS - the directory of the ICU headers
#  ICU_LIBRARIES - the ICU libraries needed for linking
#

find_path(ICU_INCLUDE_DIR unicode/ucol.h)
find_library(ICU_LIBRARY_I18N NAMES icui18n)
find_library(ICU_LIBRARY_UC NAMES icuuc)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ICU
    REQUIRED_VARS ICU_INCLUDE_DIR ICU_LIBRARY_I18N ICU_LIBRARY_UC)
set(ICU_INCLUDE_DIRS ${ICU_INCLUDE_DIR})
set(ICU_LIBRARIES ${ICU_LIBRARY_I18N} ${ICU_LIBRARY_UC})
mark_as_advanced(ICU_INCLUDE_DIR ICU_INCLUDE_DIRS
        ICU_LIBRARY_I18N ICU_LIBRARY_UC ICU_LIBRARIES)
