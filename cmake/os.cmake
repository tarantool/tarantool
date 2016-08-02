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
    find_package_message(PLATFORM "Building for Linux" "${CMAKE_SYSTEM_NAME}")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "kFreeBSD")
    set(TARGET_OS_FREEBSD 1)
    set(TARGET_OS_DEBIAN_FREEBSD 1)
# Debian/kFreeBSD uses GNU glibc.
    add_definitions("-D_GNU_SOURCE")
    add_definitions("-D_FILE_OFFSET_BITS=64")
    find_package_message(PLATFORM "Building for Debian/kFreeBSD"
        "${CMAKE_SYSTEM_NAME}")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    set(TARGET_OS_FREEBSD 1)
    find_package_message(PLATFORM "Building for FreeBSD" "${CMAKE_SYSTEM_NAME}")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(TARGET_OS_DARWIN 1)

#
# Default build type is None, which uses depends by Apple
# command line tools. Also supportting install with MacPorts.
#
    if (NOT DARWIN_BUILD_TYPE)
        set(DARWIN_BUILD_TYPE None CACHE STRING
        "Choose the type of Darwin build, options are: None, Ports."
        FORCE)
    endif()

    if (${DARWIN_BUILD_TYPE} STREQUAL "Ports")
       # Mac ports get installed into /opt/local, hence:
       include_directories("/opt/local/include")
       set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/local/lib")
    endif()

    find_package_message(PLATFORM "Building for OS X" "${CMAKE_SYSTEM_NAME}")
    find_package_message(DARWIN_BUILD_TYPE
        "DARWIN_BUILD_TYPE: ${DARWIN_BUILD_TYPE}" "${DARWIN_BUILD_TYPE}")

    # In Mac OS, the dynamic linker recognizes
    # @loader_path, @executable_path and @rpath tokens, ex:
    #   '@loder_path/lit.dylib'
    # means load lit from the same dir the requesting binary lives in.
    # Since our dynamic libraries aren't intended for static linking,
    # this is pretty much irrelevant. Disable CMake rpath features
    # altogether. Suppresses a few warnings.
    set(CMAKE_SKIP_RPATH true)
else()
    message (FATAL_ERROR "Unsupported platform -- ${CMAKE_SYSTEM_NAME}")
endif()
