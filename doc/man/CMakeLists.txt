add_custom_target(man ALL
    COMMAND ${POD2MAN} -c 'high performance key/value storage server'
	    ${CMAKE_SOURCE_DIR}/doc/man/tarantool_box.pod > ${PROJECT_BINARY_DIR}/doc/man/tarantool_box.1
    COMMAND ${POD2MAN} -c 'readline SQL-client for tarantool'
	    ${CMAKE_SOURCE_DIR}/doc/man/tarantool.pod > ${PROJECT_BINARY_DIR}/doc/man/tarantool.1
    )

install(FILES ${PROJECT_BINARY_DIR}/doc/man/tarantool_box.1 DESTINATION ${CMAKE_MAN_DIR}/man1/)
if (ENABLE_CLIENT)
    install(FILES ${PROJECT_BINARY_DIR}/doc/man/tarantool.1 DESTINATION ${CMAKE_MAN_DIR}/man1/)
endif()
