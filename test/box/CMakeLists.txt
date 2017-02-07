include_directories(${MSGPUCK_INCLUDE_DIRS})
build_module(function1 function1.c)
target_link_libraries(function1 ${MSGPUCK_LLIBRARIES})
build_module(tuple_bench tuple_bench.c)
