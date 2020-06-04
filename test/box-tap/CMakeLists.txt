# fiber.h requires tarantool_ev.h from third_party directory.
include_directories(${CMAKE_SOURCE_DIR}/third_party)

build_module(check_merge_source check_merge_source.c)
