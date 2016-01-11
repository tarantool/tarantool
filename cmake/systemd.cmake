find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(SYSTEMD "systemd")
endif()

if (NOT SYSTEMD_FOUND)
    if (WITH_SYSTEMD)
        message (FATAL_ERROR "WITH_SYSTEMD is defined, "
            "but we can't find systemd using pkg-config")
    endif()
    set(WITH_SYSTEMD "OFF")
    file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSTEMD=OFF\n")
    return()
endif()

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
file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSTEMD=ON\n")
