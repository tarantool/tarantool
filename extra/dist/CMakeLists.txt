#
# Scripts for a RPM package
#
configure_file("tarantool.init.in" "tarantool.init")

set(SYSV_INITD_PATH ${CMAKE_INSTALL_LIBDIR}/tarantool/tarantool.init)
configure_file("tarantool.service.in" "tarantool.service")

if (ENABLE_RPM)
    # chmod +x 655
    if (SYSTEMD_FOUND AND WITH_SYSTEMD)
    message (STATUS "Building RPM with SYSTEMD")
        install (FILES tarantool.service DESTINATION ${SYSTEMD_SERVICES_INSTALL_DIR}
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
        install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_LIBDIR}/tarantool/
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    else (SYSTEMD_FOUND AND WITH_SYSTEMD)
    message (STATUS "Building RPM without SYSTEMD")
        install (FILES tarantool.init DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init.d/
            RENAME tarantool
            PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    endif(SYSTEMD_FOUND AND WITH_SYSTEMD)
    install (FILES default.rpm/tarantool DESTINATION
        ${CMAKE_INSTALL_SYSCONFDIR}/sysconfig/
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
    install (FILES dist.lua DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME tarantool_ctl
    PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
endif()
