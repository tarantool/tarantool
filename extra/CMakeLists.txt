
# scripts for RPM package
#
if ("${CPACK_GENERATOR}" STREQUAL "RPM")
	install (FILES tarantool_box DESTINATION /etc/init.d)
	install (FILES tarantool_box.sh DESTINATION /bin)
endif()
