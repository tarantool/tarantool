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
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "NetBSD")
    set(TARGET_OS_NETBSD 1)
    find_package_message(PLATFORM "Building for NetBSD" "${CMAKE_SYSTEM_NAME}")
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

    # Latest versions of Homebrew wont 'link --force' for libraries, that were
    # preinstalled in system. So we'll use this dirty hack
    find_program(HOMEBREW_EXECUTABLE brew)
    if(EXISTS ${HOMEBREW_EXECUTABLE})
        execute_process(COMMAND ${HOMEBREW_EXECUTABLE} --prefix
                        OUTPUT_VARIABLE HOMEBREW_PREFIX
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        message(STATUS "Detected Homebrew install at ${HOMEBREW_PREFIX}")

        # Detecting LibReadline
        execute_process(COMMAND ${HOMEBREW_EXECUTABLE} --prefix readline
                        OUTPUT_VARIABLE HOMEBREW_READLINE
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(DEFINED HOMEBREW_READLINE)
            if (NOT DEFINED READLINE_ROOT)
                message(STATUS "Setting readline root to ${HOMEBREW_READLINE}")
                set(READLINE_ROOT "${HOMEBREW_READLINE}")
            endif()
        elseif(NOT DEFINED ${READLINE_ROOT})
            message(WARNING "Homebrew's readline isn't installed. "
                            "Work isn't guarenteed using system readline")
        endif()

        # Detecting OpenSSL
        execute_process(COMMAND ${HOMEBREW_EXECUTABLE} --prefix openssl
                        OUTPUT_VARIABLE HOMEBREW_OPENSSL
                        OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (DEFINED HOMEBREW_OPENSSL)
            if (NOT DEFINED OPENSSL_ROOT_DIR)
                message(STATUS "Setting OpenSSL root to ${HOMEBREW_OPENSSL}")
                set(OPENSSL_ROOT_DIR "${HOMEBREW_OPENSSL}")
            endif()
        elseif(NOT DEFINED OPENSSL_ROOT_DIR)
            message(WARNING "Homebrew's OpenSSL isn't installed. Work isn't "
                            "guarenteed if built with system OpenSSL")
        endif()
    endif()
else()
    message (FATAL_ERROR "Unsupported platform -- ${CMAKE_SYSTEM_NAME}")
endif()
