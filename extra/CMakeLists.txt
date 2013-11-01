
#
# Scripts for a RPM package
#
if (ENABLE_RPM)
	# chmod +x 655
	install (FILES tarantool_box DESTINATION ${CMAKE_SYSCONF_DIR}/init.d
		 PERMISSIONS
		 OWNER_READ OWNER_WRITE
		 GROUP_READ GROUP_EXECUTE
		 WORLD_READ WORLD_EXECUTE)
	# chmod +x 755
	install (FILES tarantool_multi.sh DESTINATION bin
		 PERMISSIONS
		 OWNER_READ OWNER_WRITE OWNER_EXECUTE
		 GROUP_READ GROUP_EXECUTE
		 WORLD_READ WORLD_EXECUTE)
	# chmod +x 755
	install (FILES tarantool_deploy.sh DESTINATION bin
		 PERMISSIONS
		 OWNER_READ OWNER_WRITE OWNER_EXECUTE
		 GROUP_READ GROUP_EXECUTE
		 WORLD_READ WORLD_EXECUTE)
endif()

configure_file(rpm.spec.in rpm.spec @ONLY)

if (TARGET_OS_DARWIN)
	configure_file(org.tarantool.in org.tarantool.tarantool_${TARANTOOL_PRODUCT} @ONLY)
	# chmod +x 655
	install (FILES ${CMAKE_BINARY_DIR}/extra/org.tarantool.tarantool_${TARANTOOL_PRODUCT}
                 DESTINATION share/tarantool
		 PERMISSIONS
		 OWNER_READ OWNER_WRITE
		 GROUP_READ GROUP_EXECUTE
		 WORLD_READ WORLD_EXECUTE)
endif()

add_executable(txt2c txt2c.c)
