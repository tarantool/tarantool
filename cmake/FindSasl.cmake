# Support preference of static libs by adjusting CMAKE_FIND_LIBRARY_SUFFIXES
if(BUILD_STATIC)
    set(_sasl_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a )

    find_library(SASL_LIB NAMES sasl2)
    file(GLOB_RECURSE SASL_PLUGIN_LIBRARIES "/usr/lib/sasl2/*.a" "/usr/local/lib/sasl2/*.a")
else()
    find_library(SASL_LIB NAMES sasl2)
endif()


find_path(SASL_INCLUDE_DIRS sasl/sasl.h)


if(SASL_INCLUDE_DIRS AND SASL_LIB)
  set(SASL_FOUND TRUE)
  set(SASL_LIBRARIES ${SASL_LIB} ${SASL_PLUGIN_LIBRARIES})
endif(SASL_INCLUDE_DIRS AND SASL_LIB)


if(SASL_FOUND)
  if(NOT Sasl_FIND_QUIETLY)
    message(STATUS "SASL include dir: ${SASL_INCLUDE_DIRS}")
    message(STATUS "SASL libraries: ${SASL_LIBRARIES}")
  endif(NOT Sasl_FIND_QUIETLY)
else(SASL_FOUND)
  if (Sasl_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find sasl")
  endif (Sasl_FIND_REQUIRED)
endif(SASL_FOUND)

MARK_AS_ADVANCED(SASL_INCLUDE_DIRS SASL_LIBRARIES)

# Restore the original find library ordering
if(BUILD_STATIC)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_sasl_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
endif()
