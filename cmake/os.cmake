#
# Perform operating-system specific configuration.
#
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(TARGET_OS_LINUX 1)
#
# Enable GNU glibc extentions.
    add_definitions("-D_GNU_SOURCE")
#
# On 32-bit systems, support files larger than 2GB
# (see man page for feature_test_macros).
    add_definitions("-D_FILE_OFFSET_BITS=64")
    message(STATUS "Building for Linux")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "kFreeBSD")
    set(TARGET_OS_FREEBSD 1)
    set(TARGET_OS_DEBIAN_FREEBSD 1)
# Debian/kFreeBSD uses GNU glibc.
    add_definitions("-D_GNU_SOURCE")
    add_definitions("-D_FILE_OFFSET_BITS=64")
    message(STATUS "Building for Debian/kFreeBSD")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    set(TARGET_OS_FREEBSD 1)
    message(STATUS "Building for FreeBSD")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(TARGET_OS_DARWIN 1)
    # Mac ports get installed into /opt/local, hence:
    include_directories("/opt/local/include")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/local/lib")
    message(STATUS "Building for Mac OS X")
else()
    message (FATAL_ERROR "Unsupported platform -- ${CMAKE_SYSTEM_NAME}")
endif()
