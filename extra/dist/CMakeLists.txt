# config file for tarantoolctl
if (TARGET_OS_FREEBSD)
    set(SYSCONFIG_DEFAULT "tarantool/default")
elseif (NOT IS_DIRECTORY "${CMAKE_INSTALL_SYSCONFDIR}/sysconfig")
    # Debian/Ubuntu/etc.
    set(SYSCONFIG_DEFAULT "default")
else()
    # RedHat/Fedora/etc.
    set(SYSCONFIG_DEFAULT "sysconfig")
endif()
configure_file(tarantoolctl.in tarantoolctl @ONLY)

option(ENABLE_DIST "Enable install of init scripts" OFF)

if (NOT ENABLE_DIST)
    return ()
endif()

include(systemd)

#
# tarantoolctl
#

# Default path to data in default/tarantool
if (TARGET_OS_FREEBSD)
    set(TARANTOOL_DATADIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/db/tarantool")
else()
    set(TARANTOOL_DATADIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/tarantool")
endif()
message (STATUS "tarantoolctl datadir: ${TARANTOOL_DATADIR}")
set(TARANTOOL_LOGDIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/log/tarantool")
message (STATUS "tarantoolctl logdir: ${TARANTOOL_LOGDIR}")
set(TARANTOOL_RUNDIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/run/tarantool")
message (STATUS "tarantoolctl rundir: ${TARANTOOL_RUNDIR}")
set(TARANTOOL_USER "tarantool")
set(TARANTOOL_LANGUAGE "lua")
set(SYSCONFIG_AVAILABLEDIR "tarantool/instances.available")
set(SYSCONFIG_ENABLEDDIR "tarantool/instances.enabled")
set(TARANTOOL_AVAILABLEDIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${SYSCONFIG_AVAILABLEDIR}")
set(TARANTOOL_ENABLEDDIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/${SYSCONFIG_ENABLEDDIR}")
if(NOT WITH_SYSVINIT)
    # Don't use instances.enabled on systemd distros
    set(TARANTOOL_INSTANCEDIR "${TARANTOOL_AVAILABLEDIR}")
else()
    set(TARANTOOL_INSTANCEDIR "${TARANTOOL_ENABLEDDIR}")
endif()

configure_file(default/tarantool.in default/tarantool @ONLY)
install (FILES ${CMAKE_CURRENT_BINARY_DIR}/default/tarantool DESTINATION
    ${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFIG_DEFAULT}/
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ)

# tarantoolctl itself
install (FILES ${CMAKE_CURRENT_BINARY_DIR}/tarantoolctl DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

# directories in /etc/ for tarantoolctl
install(DIRECTORY DESTINATION
    ${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFIG_AVAILABLEDIR}
)
if (WITH_SYSVINIT)
    install(DIRECTORY DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFIG_ENABLEDDIR}
    )
endif()

# an example instance script for tarantoolctl
install (FILES example.lua DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFIG_AVAILABLEDIR}
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_READ
    GROUP_READ WORLD_READ
)

# directories for data, logs and pid files
# Sic: chmod and chown are performed by rpm/deb
install(DIRECTORY DESTINATION ${TARANTOOL_DATADIR})
install(DIRECTORY DESTINATION ${TARANTOOL_LOGDIR})

# /var/run/tarantool is only needed with sysvinit
# systemd creates this directory automatically using systemd-tmpfiles
if (WITH_SYSVINIT)
    install(DIRECTORY DESTINATION ${TARANTOOL_RUNDIR})
endif()

if (NOT TARGET_OS_FREEBSD)
    set (WITH_LOGROTATE_DEFAULT ON)
endif()
option(WITH_LOGROTATE "Enable logrotate configuration" ${WITH_LOGROTATE_DEFAULT})
file(APPEND "${_OptionalPackagesFile}" "-- WITH_LOGROTATE=${WITH_LOGROTATE}\n")

if (WITH_LOGROTATE)
    # logrotate files
    configure_file(tarantool.logrotate.in tarantool.logrotate @ONLY)
    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/tarantool.logrotate
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/logrotate.d/
        RENAME "tarantool"
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ
        WORLD_READ)
endif()

# man page for tarantoolctl
pod2man (
    tarantoolctl.pod "tarantoolctl" 1
    "Tarantool instances control"
)

#
# Init scripts
#

if (WITH_SYSTEMD)
    message (STATUS "Using scripts for systemd")
    # NOTE: always install tarantool.init to ${CMAKE_INSTALL_PREFIX}/lib
    # instead of ${CMAKE_INSTALL_LIBDIR} because LIBDIR depends on the target
    # architecture, but tarantool-common is noarch package.
    set(SYSV_INITD_DIR ${CMAKE_INSTALL_PREFIX}/lib/tarantool)

    configure_file("tarantool@.service.in" "tarantool@.service" @ONLY)
    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/tarantool@.service
        DESTINATION ${SYSTEMD_UNIT_DIR}
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_READ
        WORLD_READ WORLD_READ)

    if(WITH_SYSVINIT)
        # Install generator and tarantool.service file to allow
        # sysvinit alongside systemd (Debian/Ubuntu)
        #
        configure_file("tarantool-generator.in" "tarantool-generator" @ONLY)
        install (FILES ${CMAKE_CURRENT_BINARY_DIR}/tarantool-generator
            DESTINATION ${SYSTEMD_GENERATOR_DIR}
            PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_READ WORLD_EXECUTE)
        install (FILES ${CMAKE_CURRENT_SOURCE_DIR}/tarantool.service
            DESTINATION ${SYSTEMD_UNIT_DIR}
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_READ
            WORLD_READ WORLD_READ)
    endif()

    configure_file(tarantool.tmpfiles.conf.in tarantool.tmpfiles.conf @ONLY)
    install (FILES "${CMAKE_CURRENT_BINARY_DIR}/tarantool.tmpfiles.conf"
        DESTINATION "${SYSTEMD_TMPFILES_DIR}"
        RENAME "tarantool.conf"
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_READ
        WORLD_READ WORLD_READ)
endif()

if (WITH_SYSVINIT)
    message (STATUS "Using scripts for sysvinit")
    install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init.d/
        RENAME tarantool
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
endif()
