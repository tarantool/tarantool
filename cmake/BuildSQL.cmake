option(ENABLE_PSQL  "Enable postgresql client for box.net.sql" OFF)
option(ENABLE_MYSQL "Enable mysql client for box.net.sql" OFF)



if(ENABLE_PSQL)
	include(FindPostgreSQL)
	if (PostgreSQL_FOUND)
		message(STATUS
			"box.net.sql(pg): INC=${PostgreSQL_INCLUDE_DIR}")
		message(STATUS
			"box.net.sql(pg): LIBDIR=${PostgreSQL_LIBRARY_DIR}")
		message(STATUS
			"box.net.sql(pg): LIBS=${PostgreSQL_LIBRARIES}")

		add_compile_flags("C;CXX" "-I${PostgreSQL_INCLUDE_DIR}")
		add_compile_flags("C;CXX" "-L${PostgreSQL_LIBRARY_DIR}")
		add_compile_flags("C;CXX" "-l${PostgreSQL_LIBRARIES}")

	else()
		message(STATUS "PostgreSQL client not found")
		set(ENABLE_PSQL OFF)
	endif()
endif()

if(ENABLE_PSQL)
	set(USE_PSQL_CLIENT "1")
else()
	set(USE_PSQL_CLIENT "0")
endif()


if(ENABLE_MYSQL)
	include(FindMySQL)
	if (MYSQL_FOUND)
		message(STATUS
			"box.net.sql(mysql) INC=${MYSQL_INCLUDE_DIR}")
		message(STATUS
			"box.net.sql(mysql) LIBS=mysqlclient_r")
		add_compile_flags("C;CXX" "-I${MYSQL_INCLUDE_DIR}")
		add_compile_flags("C;CXX" "-lmysqlclient_r")
	else()
		message(STATUS "MySQL client not found")
		set(ENABLE_MYSQL OFF)
	endif()
endif()

if (ENABLE_MYSQL)
	set(USE_MYSQL_CLIENT "1")
else()
	set(USE_MYSQL_CLIENT "0")
endif()
