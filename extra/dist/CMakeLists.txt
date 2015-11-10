# Scripts for a tarantoolctl packaging
set(SYSV_INITD_PATH ${CMAKE_INSTALL_LIBDIR}/tarantool/tarantool.init)
# Default path to data in default/tarantool
if (NOT TARANTOOL_DATADIR)
    set(TARANTOOL_DATADIR "/var/lib/tarantool")
endif()
message (STATUS "TARANTOOL_DATADIR: ${TARANTOOL_DATADIR}")

configure_file("tarantool.service.in" "tarantool.service")
configure_file(default/tarantool.in default/tarantool)

if (ENABLE_RPM)
    # chmod +x 655
    if (WITH_SYSTEMD)
    message (STATUS "Building RPM with SYSTEMD")
        install (FILES ${PROJECT_BINARY_DIR}/extra/dist/tarantool.service DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR}
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
        install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_LIBDIR}/tarantool/
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    else (WITH_SYSTEMD)
    message (STATUS "Building RPM without SYSTEMD")
        install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init.d/
            RENAME tarantool
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    endif(WITH_SYSTEMD)
    install (FILES ${PROJECT_BINARY_DIR}/extra/dist/default/tarantool DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/sysconfig/
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ)
else (ENABLE_RPM)
    install (FILES ${PROJECT_BINARY_DIR}/extra/dist/default/tarantool DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/default/
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ)
endif(ENABLE_RPM)

install (FILES tarantoolctl DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

install (FILES example.lua DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/tarantool/instances.available
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE
)

pod2man (
    ${CMAKE_SOURCE_DIR}/extra/dist/tarantoolctl
    "tarantoolctl" 1 extra/dist
    "tarantoolctl instances control"
)

