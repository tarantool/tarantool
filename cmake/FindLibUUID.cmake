if(NOT LIBUUID_FOUND)
    message(STATUS "NOT FOUND")

    find_path(LIBUUID_INCLUDE_DIR
        NAMES uuid.h
        PATH_SUFFIXES uuid
    )
    find_library(LIBUUID_LIBRARIES
        NAMES uuid
    )

    if (LIBUUID_INCLUDE_DIR AND LIBUUID_LIBRARIES)
        set(LIBUUID_FOUND ON)
    endif(LIBUUID_INCLUDE_DIR AND LIBUUID_LIBRARIES)

    if(LIBUUID_FOUND)
        if (NOT LIBUUID_FIND_QUIETLY)
            message(STATUS "Found libuuid includes: ${LIBUUID_INCLUDE_DIR}")
            message(STATUS "Found libuuid library: ${LIBUUID_LIBRARIES}")
        endif (NOT LIBUUID_FIND_QUIETLY)
        set(LIBUUID_INCLUDE_DIRS ${LIBUUID_INCLUDE_DIR})
    else(LIBUUID_FOUND)
        if (LIBUUID_FIND_REQUIRED)
            message(FATAL_ERROR "Could not find libuuid development files")
        endif (LIBUUID_FIND_REQUIRED)
    endif(LIBUUID_FOUND)
endif (NOT LIBUUID_FOUND)

mark_as_advanced(LIBUUID_LIBRARIES LIBUUID_INCLUDE_DIRS)
