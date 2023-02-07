build_module(module_api module_api.c)
target_link_libraries(module_api msgpuck)

# gh-5313: verify that module.h actually conforms to the C99
# standard.
set(CMAKE_C_FLAGS "-Wall -Wextra -std=c99")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Werror")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2")
