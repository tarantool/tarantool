# - Find the iconv include file and optional library
#
# ICONV_INCLUDE_DIRS
# ICONV_LIBRARIES
#

if (TARGET_OS_LINUX)
    set(ICONV_LIBRARY "")
else()
    find_library(ICONV_LIBRARY iconv)
    if(NOT ICONV_LIBRARY)
        message(WARNING "iconv library not found")
        set(ICONV_LIBRARY "")
    endif()
endif()
find_path(ICONV_INCLUDE_DIR iconv.h)
if(NOT ICONV_INCLUDE_DIR)
    message(FATAL_ERROR "iconv include header not found")
endif()
set(ICONV_LIBRARIES ${ICONV_LIBRARY})
set(ICONV_INCLUDE_DIRS ${ICONV_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${ICONV_LIBRARIES})
set(CMAKE_REQUIRED_INCLUDES ${ICONV_INCLUDE_DIRS})
check_c_source_runs("
    #include <iconv.h>
    int main()
    {
        void* foo = iconv_open(\"UTF-8\", \"ISO-8859-1\");
        iconv_close(foo);
        return 0;
    }
    " ICONV_RUNS)
set(CMAKE_REQUIRED_LIBRARIES "")
set(CMAKE_REQUIRED_INCLUDES "")
if (NOT DEFINED ICONV_RUNS_EXITCODE OR ICONV_RUNS_EXITCODE)
    unset(ICONV_LIBRARIES)
    unset(ICONV_INCLUDE_DIRS)
    set(ICONV_FOUND false)
    if (ICONV_FIND_REQUIRED)
        message(FATAL_ERROR "ICONV does not run")
    endif()
endif()

mark_as_advanced(ICONV_INCLUDE_DIRS ICONV_LIBRARIES)
