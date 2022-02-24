include_directories(${MSGPUCK_INCLUDE_DIRS})
build_module(sql_datetime sql_datetime.c)
target_link_libraries(sql_datetime msgpuck core)
