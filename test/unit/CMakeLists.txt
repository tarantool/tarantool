add_executable(queue queue.c)
add_executable(mhash mhash.c)
set_target_properties(mhash PROPERTIES COMPILE_FLAGS "-std=c99")
