add_library(gopt STATIC gopt.c)
set_target_properties(gopt PROPERTIES COMPILE_FLAGS "${core_cflags}")
