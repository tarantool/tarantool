#
# tarantoolctl
#

# Default path to data in default/tarantool
set(TARANTOOL_DATADIR "${CMAKE_INSTALL_LOCALSTATEDIR}/lib/tarantool")
message (STATUS "tarantoolctl datadir: ${TARANTOOL_DATADIR}")
set(TARANTOOL_LOGDIR "${CMAKE_INSTALL_LOCALSTATEDIR}/log/tarantool")
message (STATUS "tarantoolctl logdir: ${TARANTOOL_LOGDIR}")
set(TARANTOOL_RUNDIR "${CMAKE_INSTALL_LOCALSTATEDIR}/run/tarantool")
message (STATUS "tarantoolctl rundir: ${TARANTOOL_RUNDIR}")

# config file for tarantoolctl
if (NOT IS_DIRECTORY "${CMAKE_INSTALL_SYSCONFDIR}/sysconfig")
    # Debian/Ubuntu/etc.
    set(SYSCONFIG_DEFAULT "default")
else()
    # RedHat/Fedora/etc.
    set(SYSCONFIG_DEFAULT "sysconfig")
endif()
configure_file(default/tarantool.in default/tarantool)
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

# an example instance script for tarantoolctl
install (FILES example.lua DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/tarantool/instances.available
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_READ
    GROUP_READ GROUP_READ
    WORLD_READ WORLD_READ
)

# man page for tarantoolctl
pod2man (
    tarantoolctl "tarantoolctl" 1
    "tarantoolctl instances control"
)

#
# Init scripts
#

if (WITH_SYSTEMD)
    message (STATUS "Installing systemd scripts")
    # NOTE: always install tarantool.init to ${CMAKE_INSTALL_PREFIX}/lib
    # instead of ${CMAKE_INSTALL_LIBDIR} because LIBDIR depends on the target
    # architecture, but tarantool-common is noarch package.
    set(SYSV_INITD_DIR ${CMAKE_INSTALL_PREFIX}/lib/tarantool/)

    configure_file("tarantool.service.in" "tarantool.service")
    install (FILES ${PROJECT_BINARY_DIR}/extra/dist/tarantool.service DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR}
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    install (FILES tarantool.init DESTINATION ${SYSV_INITD_DIR}
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
elseif (WITH_SYSVINIT)
    message (STATUS "Installing sysvinit scripts")
    install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init.d/
        RENAME tarantool
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)
    install (FILES ${PROJECT_BINARY_DIR}/extra/dist/default/tarantool DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/sysconfig/
        PERMISSIONS
        OWNER_READ OWNER_WRITE
        GROUP_READ
        WORLD_READ)
endif()
