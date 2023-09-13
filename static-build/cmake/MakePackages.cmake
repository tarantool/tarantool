# CPack makes DEB and RPM packages. Creating RPM packages requires the rpm-build
# package installed. Creating DEB packages doesn't need any additional tools.
# CPack should have version 3.17 or greater (previous versions weren't tested).
# For building packages, it is recommended using OS with quite old glibc (for
# example, centos 7) for compatibility of the created packages with the various
# distros.

# Set architecture for packages (x86_64 or aarch64)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR
       CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(CPACK_COMPONENTS_GROUPING ONE_PER_GROUP)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_DEB_COMPONENT_INSTALL ON)

install(DIRECTORY tarantool-prefix/include/ DESTINATION include
        USE_SOURCE_PERMISSIONS
        COMPONENT dev
        EXCLUDE_FROM_ALL)

install(DIRECTORY tarantool-prefix/bin/ DESTINATION bin
        USE_SOURCE_PERMISSIONS
        COMPONENT server
        EXCLUDE_FROM_ALL
        FILES_MATCHING PATTERN "tarantool")

install(DIRECTORY tarantool-prefix/share/man/man1 DESTINATION share/man/
        USE_SOURCE_PERMISSIONS
        COMPONENT server
        EXCLUDE_FROM_ALL
        FILES_MATCHING PATTERN "tarantool.1")

install(FILES ../README.md
        DESTINATION /usr/share/doc/tarantool
        COMPONENT server)

if (PACKAGE_FORMAT STREQUAL "DEB")
    install(FILES ../AUTHORS ../debian/copyright ../LICENSE
            DESTINATION /usr/share/doc/tarantool
            COMPONENT server)
    install(FILES ../AUTHORS ../debian/copyright ../LICENSE
            DESTINATION /usr/share/doc/tarantool-dev
            COMPONENT dev)
endif()

if (PACKAGE_FORMAT STREQUAL "RPM")
    install(FILES ../AUTHORS ../LICENSE
            DESTINATION /usr/share/licenses/tarantool
            COMPONENT server)
    install(FILES ../AUTHORS ../LICENSE
            DESTINATION /usr/share/licenses/tarantool-devel
            COMPONENT dev)
endif()

set(CPACK_GENERATOR "DEB;RPM")

set(CPACK_PACKAGE_NAME "tarantool")
set(CPACK_PACKAGE_CONTACT "admin@tarantool.org")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://tarantool.org")
set(CPACK_PACKAGE_VERSION "$ENV{VERSION}")
set(CPACK_PACKAGE_DIRECTORY "$ENV{OUTPUT_DIR}")

set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "BSD")

set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Tarantool Team <admin@tarantool.org>")

set(TARANTOOL_SERVER_DESCRIPTION
"Tarantool is a high performance in-memory database and Lua application server.
Tarantool supports replication, online backup and stored procedures in Lua.
The package provides Tarantool server binary.")

set(TARANTOOL_DEV_DESCRIPTION
"Tarantool is a high performance in-memory database and Lua application server.
Tarantool supports replication, online backup and stored procedures in Lua.
The package provides Tarantool server development files needed for pluggable
modules.")

set(CPACK_RPM_SERVER_PACKAGE_NAME "tarantool")
set(CPACK_RPM_SERVER_PACKAGE_GROUP "Applications/Databases")
set(CPACK_RPM_SERVER_PACKAGE_SUMMARY "In-memory database and Lua application server")
set(CPACK_RPM_SERVER_FILE_NAME
    tarantool-${CPACK_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${CPACK_RPM_PACKAGE_ARCHITECTURE}.rpm)

set(CPACK_RPM_DEV_PACKAGE_NAME "tarantool-devel")
set(CPACK_RPM_DEV_PACKAGE_GROUP "Applications/Databases")
set(CPACK_RPM_DEV_PACKAGE_SUMMARY "Tarantool server development files")
set(CPACK_RPM_DEV_FILE_NAME
    tarantool-devel-${CPACK_PACKAGE_VERSION}-${CPACK_RPM_PACKAGE_RELEASE}.${CPACK_RPM_PACKAGE_ARCHITECTURE}.rpm)

set(CPACK_DEBIAN_SERVER_PACKAGE_NAME "tarantool")
set(CPACK_DEBIAN_SERVER_PACKAGE_SECTION "database")
set(CPACK_DEBIAN_SERVER_FILE_NAME
    tarantool_${CPACK_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb)

set(CPACK_DEBIAN_DEV_PACKAGE_NAME "tarantool-dev")
set(CPACK_DEBIAN_DEV_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_DEV_FILE_NAME
    tarantool-dev_${CPACK_PACKAGE_VERSION}-${CPACK_DEBIAN_PACKAGE_RELEASE}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb)

include(CPack)

cpack_add_component(dev
                    DISPLAY_NAME "Tarantool server development files"
                    DESCRIPTION ${TARANTOOL_DEV_DESCRIPTION}
                    GROUP dev)

cpack_add_component(server
                    DISPLAY_NAME "Tarantool server binary"
                    DESCRIPTION ${TARANTOOL_SERVER_DESCRIPTION}
                    GROUP server)

cpack_add_component_group(dev)
cpack_add_component_group(server)
