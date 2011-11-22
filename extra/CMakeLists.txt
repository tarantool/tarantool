
# scripts for RPM package
#
if ("${CPACK_GENERATOR}" STREQUAL "RPM")
	install (FILES tarantool_box DESTINATION /etc/init.d
                 PERMISSIONS OWNER_EXECUTE)
	install (FILES tarantool_box.sh DESTINATION bin
                 PERMISSIONS OWNER_EXECUTE)
endif()
