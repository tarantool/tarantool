
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

add_executable(txt2c txt2c.c)
add_executable(bin2c bin2c.c)
