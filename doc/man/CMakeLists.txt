add_custom_target(man ALL
    COMMAND ${POD2MAN} -c 'Lua application server and NoSQL database'
	    ${CMAKE_SOURCE_DIR}/doc/man/tarantool.pod > ${PROJECT_BINARY_DIR}/doc/man/tarantool.1
    )
install(FILES ${PROJECT_BINARY_DIR}/doc/man/tarantool.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1/)
