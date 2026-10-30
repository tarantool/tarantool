# Disable systemd and sysvinit by default if target is not Linux
if (NOT DEFINED WITH_SYSTEMD AND NOT TARGET_OS_LINUX)
    set(WITH_SYSTEMD OFF)
endif()
if (NOT DEFINED WITH_SYSVINIT AND NOT TARGET_OS_LINUX)
    set(WITH_SYSVINIT OFF)
endif()

# try to find systemd if it wasn't implicitly disabled
if (NOT DEFINED WITH_SYSTEMD OR WITH_SYSTEMD)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(SYSTEMD "systemd")
    endif()
endif()

if (NOT SYSTEMD_FOUND AND NOT WITH_SYSTEMD)
    set(WITH_SYSTEMD "OFF")
    # Fallback to sysvinit if it wasn't implicitly enabled
    if (NOT DEFINED WITH_SYSVINIT)
        set(WITH_SYSVINIT ON)
    endif()
endif()

if (WITH_SYSTEMD)
    if ("${SYSTEMD_UNIT_DIR}" STREQUAL "")
        if (SYSTEMD_FOUND)
            execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
                --variable=systemdsystemunitdir systemd
                OUTPUT_VARIABLE SYSTEMD_UNIT_DIR)
            string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_UNIT_DIR
                "${SYSTEMD_UNIT_DIR}")
        elseif(NOT CMAKE_CROSSCOMPILING AND EXISTS "/etc/debian_version")
            # Debian and Ubuntu
            set(SYSTEMD_UNIT_DIR "/lib/systemd/system")
        else()
            # RHEL, Fedora and other
            set(SYSTEMD_TMPFILES_DIR "/usr/lib/systemd/system")
        endif()
    endif()

    if ("${SYSTEMD_GENERATOR_DIR}" STREQUAL "")
        if (SYSTEMD_FOUND)
            execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
                --variable=systemdsystemgeneratordir systemd
                OUTPUT_VARIABLE SYSTEMD_GENERATOR_DIR)
            string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_GENERATOR_DIR
                "${SYSTEMD_GENERATOR_DIR}")
        elseif(NOT CMAKE_CROSSCOMPILING AND EXISTS "/etc/debian_version")
            # Debian and Ubuntu
            set(SYSTEMD_GENERATOR_DIR "/lib/systemd/system-generators")
        else()
            # RHEL, Fedora and other
            set(SYSTEMD_GENERATOR_DIR "/usr/lib/systemd/system-generators")
        endif()
    endif()

    if ("${SYSTEMD_TMPFILES_DIR}" STREQUAL "")
        set(SYSTEMD_TMPFILES_DIR "/usr/lib/tmpfiles.d")
    endif()

    set(WITH_SYSTEMD "ON")
    message(STATUS "SYSTEMD_UNIT_DIR: ${SYSTEMD_UNIT_DIR}")
    message(STATUS "SYSTEMD_GENERATOR_DIR: ${SYSTEMD_GENERATOR_DIR}")
    message(STATUS "SYSTEMD_TMPFILES_DIR: ${SYSTEMD_TMPFILES_DIR}")
    # Disable sysvinit if it wasn't implicitly requested
    if (NOT DEFINED WITH_SYSVINIT)
        set(WITH_SYSVINIT "OFF")
    endif()
endif()
file(APPEND "${_OptionalPackagesFile}" "\n")
file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSTEMD=${WITH_SYSTEMD}\n")
file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSVINIT=${WITH_SYSVINIT}\n")
