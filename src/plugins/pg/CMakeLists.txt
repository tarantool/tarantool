include(FindPostgreSQL)
if (PostgreSQL_FOUND)
	message(STATUS "box.net.sql(pg): INC=${PostgreSQL_INCLUDE_DIR}")
	message(STATUS "box.net.sql(pg): LIBDIR=${PostgreSQL_LIBRARY_DIR}")
	message(STATUS "box.net.sql(pg): LIBS=${PostgreSQL_LIBRARIES}")

	add_compile_flags("C;CXX" "-I${PostgreSQL_INCLUDE_DIR}")
	add_compile_flags("C;CXX" "-L${PostgreSQL_LIBRARY_DIR}")
	add_compile_flags("C;CXX" "-l${PostgreSQL_LIBRARIES}")
	add_library(pg SHARED pg.cc)
	install(TARGETS pg LIBRARY DESTINATION ${PLUGIN_DIR})

else()
	message(STATUS "PostgreSQL client not found")
endif()

