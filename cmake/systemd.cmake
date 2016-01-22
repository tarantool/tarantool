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

if (NOT SYSTEMD_FOUND)
    # systemd was implicitly requsted by the user
    if (WITH_SYSTEMD)
        message (FATAL_ERROR "WITH_SYSTEMD is defined, "
            "but we can't find systemd using pkg-config")
    endif()
    set(WITH_SYSTEMD "OFF")
    # Fallback to sysvinit if it wasn't implicitly enabled
    if (NOT DEFINED WITH_SYSVINIT)
        set(WITH_SYSVINIT ON)
    endif()    
else()
    if ("${SYSTEMD_UNIT_DIR}" STREQUAL "")
        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=systemdsystemunitdir systemd
            OUTPUT_VARIABLE SYSTEMD_UNIT_DIR)
        string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_UNIT_DIR
            "${SYSTEMD_UNIT_DIR}")
    endif()

    if ("${SYSTEMD_TMPFILES_DIR}" STREQUAL "")
        # NOTE: don't use ${CMAKE_INSTALL_LIBDIR} here
        set(SYSTEMD_TMPFILES_DIR "${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d")
    endif()

    set(WITH_SYSTEMD "ON")
    message(STATUS "SYSTEMD_UNIT_DIR: ${SYSTEMD_UNIT_DIR}")
    message(STATUS "SYSTEMD_TMPFILES_DIR: ${SYSTEMD_TMPFILES_DIR}")
    # Disable sysvinit if it wasn't implicitly requested
    if (NOT DEFINED WITH_SYSVINIT)
        set(WITH_SYSVINIT "OFF")
    endif()
endif()
file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSTEMD=${WITH_SYSTEMD}\n")
file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSVINIT=${WITH_SYSVINIT}\n")
