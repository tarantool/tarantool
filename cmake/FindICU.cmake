# - Find ICU header and library
# The module defines the following variables:
#
#  ICU_FOUND - true if ICU was found
#  ICU_INCLUDE_DIRS - the directory of the ICU headers
#  ICU_LIBRARIES - the ICU libraries needed for linking
#

if(DEFINED ICU_ROOT)
    set(ICU_FIND_OPTS NO_CMAKE NO_CMAKE_SYSTEM_PATH)
    set(ICU_FIND_LIBRARY_HINTS "${ICU_ROOT}/lib")
    set(ICU_FIND_PATH_HINTS "${ICU_ROOT}/include")
else()
    set(ICU_FIND_OPTS)
    set(ICU_FIND_LIBRARY_HINTS)
    set(ICU_FIND_PATH_HINTS)
endif()

find_path(ICU_INCLUDE_DIR
    unicode/ucol.h
    HINTS ${ICU_FIND_PATH_HINTS}
    ${ICU_FIND_OPTS}
)
find_library(ICU_LIBRARY_I18N NAMES icui18n
    HINTS ${ICU_FIND_LIBRARY_HINTS}
    ${ICU_FIND_OPTS}
)
find_library(ICU_LIBRARY_UC NAMES icuuc
    HINTS ${ICU_FIND_LIBRARY_HINTS}
    ${ICU_FIND_OPTS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ICU
    REQUIRED_VARS ICU_INCLUDE_DIR ICU_LIBRARY_I18N ICU_LIBRARY_UC)
set(ICU_INCLUDE_DIRS ${ICU_INCLUDE_DIR})
set(ICU_LIBRARIES ${ICU_LIBRARY_I18N} ${ICU_LIBRARY_UC})
mark_as_advanced(ICU_INCLUDE_DIR ICU_INCLUDE_DIRS
        ICU_LIBRARY_I18N ICU_LIBRARY_UC ICU_LIBRARIES)

#
# Check presence of ucol_strcollUTF8 function from ICU
#
set(CMAKE_REQUIRED_LIBRARIES ${ICU_LIBRARIES})
set(CMAKE_REQUIRED_INCLUDES ${ICU_INCLUDE_DIRS})
set(CMAKE_REQUIRED_FLAGS "-std=c++11")
set(CMAKE_REQUIRED_DEFINITIONS "")
check_cxx_source_compiles("#include <unicode/ucol.h>
int main(void) {
    return (int)ucol_strcollUTF8(0, 0, 0, 0, 0, 0);
}" HAVE_ICU_STRCOLLUTF8)
set(CMAKE_REQUIRED_LIBRARIES "")
set(CMAKE_REQUIRED_INCLUDES "")
set(CMAKE_REQUIRED_FLAGS "")
