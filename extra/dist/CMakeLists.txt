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
configure_file(default/tarantool.in default/tarantool @ONLY)
install (FILES ${PROJECT_BINARY_DIR}/extra/dist/default/tarantool DESTINATION
    ${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFIG_DEFAULT}/
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ)

# tarantoolctl itself
install (FILES tarantoolctl DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

# directories in /etc/ for tarantoolctl
install(DIRECTORY DESTINATION
    ${CMAKE_INSTALL_SYSCONFDIR}/tarantool/instances.enabled
)
install(DIRECTORY DESTINATION
    ${CMAKE_INSTALL_SYSCONFDIR}/tarantool/instances.available
)

# an example instance script for tarantoolctl
install (FILES example.lua DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/tarantool/instances.available
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_READ
    GROUP_READ WORLD_READ
)

# directories for data, logs and pid files
# Sic: chmod and chown are performed by rpm/deb
install(DIRECTORY DESTINATION ${TARANTOOL_DATADIR})
install(DIRECTORY DESTINATION ${TARANTOOL_LOGDIR})
install(DIRECTORY DESTINATION ${TARANTOOL_RUNDIR})

if (NOT TARGET_OS_FREEBSD)
# logrotate files
configure_file(tarantool.logrotate.in tarantool.logrotate @ONLY)
install (FILES ${PROJECT_BINARY_DIR}/extra/dist/tarantool.logrotate
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/logrotate.d/
    RENAME "tarantool"
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ)
endif()

# man page for tarantoolctl
pod2man (
    tarantoolctl "tarantoolctl" 1
    "tarantoolctl instances control"
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

    configure_file("tarantool.service.in" "tarantool.service" @ONLY)
    install (FILES ${PROJECT_BINARY_DIR}/extra/dist/tarantool.service
        DESTINATION ${SYSTEMD_UNIT_DIR}
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_READ
        WORLD_READ WORLD_READ)
    install (FILES tarantool.init DESTINATION ${SYSV_INITD_DIR}
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    configure_file(tarantool.tmpfiles.conf.in tarantool.tmpfiles.conf @ONLY)
    install (FILES "${PROJECT_BINARY_DIR}/extra/dist/tarantool.tmpfiles.conf"
        DESTINATION "${SYSTEMD_TMPFILES_DIR}"
        RENAME "tarantool.conf"
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_READ
        WORLD_READ WORLD_READ)
endif()

if(WITH_SYSVINIT)
    message (STATUS "Using scripts for sysvinit")
    install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init.d/
        RENAME tarantool
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    install (FILES ${PROJECT_BINARY_DIR}/extra/dist/default/tarantool DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/${SYSCONFIG_DEFAULT}/
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ
        WORLD_READ)
endif()
