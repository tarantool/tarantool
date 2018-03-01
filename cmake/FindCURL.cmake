# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#.rst:
# FindCURL
# --------
#
# Find curl
#
# Find the native CURL headers and libraries.
#
# ::
#
#   CURL_INCLUDE_DIRS   - where to find curl/curl.h, etc.
#   CURL_LIBRARIES      - List of libraries when using curl.
#   CURL_FOUND          - True if curl found.
#   CURL_VERSION_STRING - the version of curl found (since CMake 2.8.8)

if(DEFINED CURL_ROOT)
    set(CURL_FIND_OPTS NO_CMAKE NO_CMAKE_SYSTEM_PATH)
    set(CURL_FIND_LIBRARY_HINTS "${CURL_ROOT}/lib")
    set(CURL_FIND_PATH_HINTS "${CURL_ROOT}/include")
else()
    set(CURL_FIND_OPTS)
    set(CURL_FIND_LIBRARY_HINTS)
    set(CURL_FIND_PATH_HINTS)
endif()

# Look for the header file.
find_path(CURL_INCLUDE_DIR NAMES
    curl/curl.h
    HINTS ${CURL_FIND_PATH_HINTS}
    ${CURL_FIND_OPTS}
)
mark_as_advanced(CURL_INCLUDE_DIR)

# Look for the library (sorted from most current/relevant entry to least).
find_library(CURL_LIBRARY NAMES
    curl
  # Windows MSVC prebuilts:
    curllib
    libcurl_imp
    curllib_static
  # Windows older "Win32 - MSVC" prebuilts (libcurl.lib, e.g. libcurl-7.15.5-win32-msvc.zip):
    libcurl
    HINTS ${CURL_FIND_LIBRARY_HINTS}
    ${CURL_FIND_OPTS}
)
mark_as_advanced(CURL_LIBRARY)

if(CURL_INCLUDE_DIR)
  foreach(_curl_version_header curlver.h curl.h)
    if(EXISTS "${CURL_INCLUDE_DIR}/curl/${_curl_version_header}")
      file(STRINGS "${CURL_INCLUDE_DIR}/curl/${_curl_version_header}" curl_version_str REGEX "^#define[\t ]+LIBCURL_VERSION[\t ]+\".*\"")

      string(REGEX REPLACE "^#define[\t ]+LIBCURL_VERSION[\t ]+\"([^\"]*)\".*" "\\1" CURL_VERSION_STRING "${curl_version_str}")
      unset(curl_version_str)
      break()
    endif()
  endforeach()
endif()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(CURL
                                  REQUIRED_VARS CURL_LIBRARY CURL_INCLUDE_DIR
                                  VERSION_VAR CURL_VERSION_STRING)

if(CURL_FOUND)
  set(CURL_LIBRARIES ${CURL_LIBRARY})
  set(CURL_INCLUDE_DIRS ${CURL_INCLUDE_DIR})
  set(CMAKE_REQUIRED_LIBRARIES ${CURL_LIBRARIES})
  set(CMAKE_REQUIRED_INCLUDES ${CURL_INCLUDE_DIRS})
  check_c_source_runs("
    #include <curl/curl.h>

    int main()
    {
    #ifdef CURL_VERSION_SSL
        curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
        if (data->features & CURL_VERSION_SSL)
            return 0;
    #endif
        return -1;
    }
    " CURL_SUPPORTS_SSL)
  set(CMAKE_REQUIRED_LIBRARIES "")
  set(CMAKE_REQUIRED_INCLUDES "")
    if (NOT DEFINED CURL_SUPPORTS_SSL_EXITCODE OR CURL_SUPPORTS_SSL_EXITCODE)
        unset(CURL_LIBRARIES)
        unset(CURL_INCLUDE_DIRS)
        set(CURL_FOUND false)
        if (CURL_FIND_REQUIRED)
            message(FATAL_ERROR "Curl was built without SSL support")
        else()
            message(WARNING "Curl was built without SSL support")
        endif()
    endif()
endif()
