find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(SYSTEMD "systemd")
    
    if (SYSTEMD_FOUND AND "${SYSTEMD_SERVICES_INSTALL_DIR}" STREQUAL "")
        execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE}
            --variable=systemdsystemunitdir systemd
            OUTPUT_VARIABLE SYSTEMD_SERVICES_INSTALL_DIR)
        string(REGEX REPLACE "[ \t\n]+" "" SYSTEMD_SERVICES_INSTALL_DIR
            "${SYSTEMD_SERVICES_INSTALL_DIR}")
    elseif (NOT SYSTEMD_FOUND AND SYSTEMD_SERVICES_INSTALL_DIR)
        message (FATAL_ERROR "Variable SYSTEMD_SERVICES_INSTALL_DIR is\
            defined, but we can't find systemd using pkg-config")
    endif()

    if (SYSTEMD_FOUND)
        message(STATUS "SYSTEMD_SERVICES_INSTALL_DIR: ${SYSTEMD_SERVICES_INSTALL_DIR}")
    else()
        set(WITH_SYSTEMD "OFF")
    endif (SYSTEMD_FOUND)
else(PKG_CONFIG_FOUND)
    set(WITH_SYSTEMD "OFF")
endif(PKG_CONFIG_FOUND)

file(APPEND "${_OptionalPackagesFile}" "-- WITH_SYSTEMD=${WITH_SYSTEMD}\n")
