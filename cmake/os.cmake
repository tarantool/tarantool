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

    # There are some subtle differences in Linux kernel calls
    # implementation under WSL1 (which should go away with WSL2 kernel)
    # so for a moment we introduce a way to distinguish Linux and
    # Microsoft/WSL1
    if (${CMAKE_SYSTEM} MATCHES "Linux-.*-Microsoft")
        add_definitions("-DTARANTOOL_WSL1_WORKAROUND_ENABLED=1")
    endif()

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

    # FreeBSD has OpenSSL library installed in /usr as part of a
    # base system. A user may install OpenSSL from ports / pkg to
    # /usr/local. It is tricky to use the library from /usr in the
    # case, because a compilation unit can also depend on
    # libraries from /usr/local. When -I/usr/local/include is
    # passed to a compiler it will find openssl/ssl.h from
    # /usr/local/include first.
    #
    # In theory we can create a directory on the build stage and
    # fill it with symlinks to choosen headers. However this way
    # does not look as usual way to pick libraries to build
    # against. I suspect that this is common problem on FreeBSD
    # and we should wait for some general resolution from FreeBSD
    # developers rather then work it around.
    #
    # Verify that /usr is not set as a directory to pick OpenSSL
    # library and header files, because it is likely that a user
    # set it to use the library from a base system, while the
    # library is also installed into /usr/local.
    #
    # It is possible however that a user is aware of the problem,
    # but want to use -DOPENSSL_ROOT_DIR=<...> CMake option to
    # choose OpenSSL from /usr anyway. We should not fail the
    # build and block this ability. Say, a user may know that
    # there are no OpenSSL libraries in /usr/local, but finds it
    # convenient to set the CMake option explicitly due to some
    # external reason.
    get_filename_component(REAL_OPENSSL_ROOT_DIR "${OPENSSL_ROOT_DIR}"
                           REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
    if ("${REAL_OPENSSL_ROOT_DIR}" STREQUAL "/usr")
        message(WARNING "Using OPENSSL_ROOT_DIR on FreeBSD to choose base "
                        "system libraries is not supported")
    endif()
    unset(REAL_OPENSSL_ROOT_DIR)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "NetBSD")
    set(TARGET_OS_NETBSD 1)
    find_package_message(PLATFORM "Building for NetBSD" "${CMAKE_SYSTEM_NAME}")
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
    set(TARGET_OS_OPENBSD 1)
    set(TARGET_OS_FREEBSD 1)
    find_package_message(PLATFORM "Building for OpenBSD" "${CMAKE_SYSTEM_NAME}")
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

    if (NOT BUILD_STATIC)
        find_program(HOMEBREW_EXECUTABLE brew)
    endif()
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
        execute_process(COMMAND ${HOMEBREW_EXECUTABLE} --prefix openssl@1.1
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

        # Detecting CURL
        execute_process(COMMAND ${HOMEBREW_EXECUTABLE} --prefix curl
            OUTPUT_VARIABLE HOMEBREW_CURL
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (DEFINED HOMEBREW_CURL)
            if (NOT DEFINED CURL_ROOT_DIR)
                message(STATUS "Setting CURL root to ${HOMEBREW_CURL}")
                set(CURL_ROOT ${HOMEBREW_CURL})
            endif()
        elseif(NOT DEFINED CURL_ROOT_DIR)
            message(WARNING "Homebrew's CURL isn't installed. Work isn't "
                "guarenteed if built with system CURL")
        endif()

        # Detecting ICU4C
        if (NOT DEFINED ICU_ROOT)
            execute_process(COMMAND ${HOMEBREW_EXECUTABLE} --prefix icu4c
                OUTPUT_VARIABLE HOMEBREW_ICU4C
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if (HOMEBREW_ICU4C)
                message(STATUS "Setting ICU root to ${HOMEBREW_ICU4C}")
                set(ICU_ROOT ${HOMEBREW_ICU4C})
            endif()
        endif()
    endif()
else()
    message (FATAL_ERROR "Unsupported platform -- ${CMAKE_SYSTEM_NAME}")
endif()
