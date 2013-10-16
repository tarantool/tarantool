find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql
)
find_library(MYSQL_LIBRARIES
    NAMES mysqlclient_r
    PATH_SUFFIXES mysql
)

if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARIES)
    set(MYSQL_FOUND ON)
endif(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARIES)

if(MYSQL_FOUND)
    if (NOT MYSQL_FIND_QUIETLY)
        message(STATUS "Found MySQL includes: ${MYSQL_INCLUDE_DIR}/mysql.h")
        message(STATUS "Found MySQL library: ${MYSQL_LIBRARIES}")
    endif (NOT MYSQL_FIND_QUIETLY)
    set(MYSQL_INCLUDE_DIRS ${MYSQL_INCLUDE_DIR})
else(MYSQL_FOUND)
    if (MYSQL_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find mysql development files")
    endif (MYSQL_FIND_REQUIRED)
endif (MYSQL_FOUND)
