# Current we are not support GNUstep libraries
find_path(LIBOBJC_INCLUDE_DIR NAMES objc/Object.h)
find_library(LIBOBJC_LIBRARIES NAMES objc)

if(LIBOBJC_INCLUDE_DIR AND LIBOBJC_LIBRARIES)
    set(LIBOBJC_FOUND ON)
endif(LIBOBJC_INCLUDE_DIR AND LIBOBJC_LIBRARIES)

if(LIBOBJC_FOUND)
    if (NOT LIBOBJC_FIND_QUIETLY)
        message(STATUS "Found libobjc includes: ${LIBOBJC_INCLUDE_DIR}/objc")
        message(STATUS "Found libobjc library: ${LIBOBJC_LIBRARIES}")
    endif (NOT LIBOBJC_FIND_QUIETLY)
else(LIBOBJC_FOUND)
    if (LIBOBJC_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find libobjc development files")
    endif (LIBOBJC_FIND_REQUIRED)
endif (LIBOBJC_FOUND)
