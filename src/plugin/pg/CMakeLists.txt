# A special hack for buggy FindPostgreSQL.cmake in Debian/Ubuntu
set(PostgreSQL_ROOT /usr/include/postgresql;/usr/include/postgres)
find_package(PostgreSQL)
if (PostgreSQL_FOUND)
	message(STATUS "box.net.sql(pg): INC=${PostgreSQL_INCLUDE_DIRS}")
	message(STATUS "box.net.sql(pg): LIBS=${PostgreSQL_LIBRARIES}")

	include_directories(${PostgreSQL_INCLUDE_DIRS})
	add_library(pg SHARED pg.cc)
	target_link_libraries(pg ${PostgreSQL_LIBRARIES})
	install(TARGETS pg LIBRARY DESTINATION ${PLUGIN_DIR})

else()
	message(STATUS "PostgreSQL client not found")
endif()

