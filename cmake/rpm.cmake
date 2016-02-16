find_program(RPMBUILD rpmbuild)
if(NOT IS_DIRECTORY ${PROJECT_SOURCE_DIR}/rpm)
    return()
endif()
if (RPMBUILD)
    find_program(MKDIR mkdir)
    find_program(CP cp)
    find_program(WC wc)

    set (SCL_VERSION "1.0"         CACHE STRING "" FORCE)
    set (SCL_RELEASE "1"           CACHE STRING "" FORCE)
    set (SCL_TARANTOOL "mailru-16" CACHE STRING "" FORCE)

    set (RPM_SOURCE_DIRECTORY_NAME ${CPACK_SOURCE_PACKAGE_FILE_NAME}
        CACHE STRING "" FORCE)
    set (RPM_PACKAGE_SOURCE_FILE_NAME ${PACKAGE_VERSION}.tar.gz
        CACHE STRING "" FORCE)

    set (RPM_BUILDROOT "${PROJECT_BINARY_DIR}/RPM/BUILDROOT" CACHE STRING "" FORCE)

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/tarantool-${PACKAGE_VERSION}.tar.gz
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        COMMAND $(MAKE) package_source)

    add_custom_command(OUTPUT ${RPM_BUILDROOT}
        COMMAND ${MKDIR} -p ${RPM_BUILDROOT})

    set(RPM_SPEC "${PROJECT_BINARY_DIR}/rpm/tarantool-${PACKAGE_VERSION}.spec")
    add_custom_target(rpm_spec
        DEPENDS ${PROJECT_BINARY_DIR}/tarantool-${PACKAGE_VERSION}.tar.gz
        COMMAND ${CMAKE_COMMAND} -E copy rpm/tarantool.spec ${RPM_SPEC}.tmp
        COMMAND sed -i -e "'s/Version:\\([ ]*\\).*/Version: ${PACKAGE_VERSION}/g'" ${RPM_SPEC}.tmp
        COMMAND ${CMAKE_COMMAND} -E rename ${RPM_SPEC}.tmp ${RPM_SPEC}
        WORKING_DIRECTORY ${SOURCE_BINARY_DIR}
    )

    add_custom_target(rpm_src
        DEPENDS rpm_spec
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --define '_sourcedir ./' --define '_srcrpmdir ./' -bs ${RPM_SPEC}
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    # TODO: Targets below are temporary broken
    return()

    add_custom_target(rpm
        DEPENDS rpm_src
        DEPENDS ${RPM_BUILDROOT}
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --rebuild ${PROJECT_BINARY_DIR}/tarantool-${PACKAGE_VERSION}.src.rpm
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    add_custom_target(rpm_systemd
        DEPENDS rpm_src
        DEPENDS ${RPM_BUILDROOT}
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --with systemd --rebuild ${PROJECT_BINARY_DIR}/tarantool-${PACKAGE_VERSION}.src.rpm
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    add_custom_target(rpm_scl_full_old
        DEPENDS ${RPM_BUILDROOT}
        DEPENDS ${PROJECT_BINARY_DIR}/${CPACK_SOURCE_PACKAGE_FILE_NAME}.tar.gz
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} -bb ${PROJECT_SOURCE_DIR}/extra/rpm/tarantool-scl.rpm.spec
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --define '_sourcedir ./' -bb ${PROJECT_SOURCE_DIR}/extra/rpm/tarantool.rpm.spec --define 'scl ${SCL_TARANTOOL}'
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    add_custom_target(rpm_scl_src
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --define '_srcrpmdir ./' -bs ${PROJECT_SOURCE_DIR}/extra/rpm/tarantool-scl.rpm.spec
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    add_custom_target(rpm_scl_noarch
        DEPENDS rpm_scl_src
        DEPENDS ${RPM_BUILDROOT}
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --rebuild ${PROJECT_BINARY_DIR}/tarantool-${SCL_TARANTOOL}-${SCL_VERSION}-${SCL_RELEASE}.src.rpm
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    add_custom_target(rpm_scl_arch
        DEPENDS rpm_src
        DEPENDS ${RPM_BUILDROOT}
        COMMAND ${RPMBUILD} --buildroot ${RPM_BUILDROOT} --rebuild ${PROJECT_BINARY_DIR}/tarantool-${VERSION}-${RELEASE}.src.rpm --define 'scl ${SCL_TARANTOOL}'
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        )

    add_custom_target(rpm_scl
        DEPENDS rpm_scl_noarch
        DEPENDS rpm_scl_arch
        )

    # TODO: Add MOCK builds
    #     : -DMOCK_TARGET
    #     : -DMOCK_OS: EPEL / FEDORA

endif()
