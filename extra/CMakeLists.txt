add_subdirectory(dist)
add_subdirectory(luarocks)

if (TARGET_OS_DARWIN)
	# NOTE: need add execution 'plutil -lint org.tarantool.tarantool.plist
	# to check syntax of plist file.
	# Also cmake doesn't support changing package id from 'com.' to 'org.'
	# Need chage to 'org.' after update lines this file:
	# https://github.com/Kitware/CMake/blob/v2.8.11.2/Source/CPack/cmCPackPackageMakerGenerator.cxx#L763
	#
	# ^^^ DO NOT CHANGE DARWIN_PACKAGE_ID BEFORE SEE URL ABOVE ^^^
	#
	set (DARWIN_PACKAGE_ID "com")
	set (LUANCHD_PLIST "${DARWIN_PACKAGE_ID}.${CPACK_PACKAGE_VENDOR}.tarantool.plist")
	# Configure scripts for *.pkg and luanchd daemon by templates
	configure_file(dmg/postflight.in postflight @ONLY)
	configure_file(org.tarantool.in ${LUANCHD_PLIST} @ONLY)
	# chmod +x 644
	install (FILES ${CMAKE_BINARY_DIR}/extra/${LUANCHD_PLIST}
                 DESTINATION share/tarantool
         PERMISSIONS
         OWNER_READ OWNER_WRITE
         GROUP_READ GROUP_READ
         WORLD_READ WORLD_READ)
endif()

if (CMAKE_CROSSCOMPILING)
    add_custom_target(txt2c
        COMMAND ${CMAKE_HOST_C_COMPILER} txt2c.c
            -o "${CMAKE_CURRENT_BINARY_DIR}/txt2c"
        DEPENDS txt2c.c)
    add_custom_target(bin2c
        COMMAND ${CMAKE_HOST_C_COMPILER} bin2c.c
            -o "${CMAKE_CURRENT_BINARY_DIR}/bin2c"
        DEPENDS bin2c.c)
    add_custom_target(lemon
        COMMAND ${CMAKE_HOST_C_COMPILER} lemon.c
            -o "${CMAKE_CURRENT_BINARY_DIR}/lemon"
        DEPENDS lemon.c)
    add_custom_target(mkkeywordhash
        COMMAND ${CMAKE_HOST_C_COMPILER} mkkeywordhash.c
            -o "${CMAKE_CURRENT_BINARY_DIR}/mkkeywordhash"
        DEPENDS mkkeywordhash.c)
else()
    add_executable(txt2c txt2c.c)
    add_executable(bin2c bin2c.c)
    add_executable(lemon lemon.c)
    add_executable(mkkeywordhash mkkeywordhash.c)
endif()
