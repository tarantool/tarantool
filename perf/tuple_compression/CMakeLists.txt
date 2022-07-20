set(CMAKE_CXX_STANDARD 11)

include_directories(${MSGPUCK_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/perf)

if (ENABLE_TUPLE_COMPRESSION)
    add_library(bench SHARED tuple_compression.cc)
    # -fPIC is required since we are going to load this lib during Tarantool runtime.
    set_target_properties(bench PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Don't mangle module name as we are going to load it manually in Lua script
    set_target_properties(bench PROPERTIES PREFIX "")
    configure_file(launch.lua.in launch.lua)
endif()
