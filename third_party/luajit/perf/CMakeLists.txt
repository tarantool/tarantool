# Running various bench suites against LuaJIT.

include(MakeLuaPath)

find_program(SH sh)
if(NOT SH)
  message(FATAL_ERROR "`sh' is not found")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(WARNING "LuaJIT and perf tests are built in the Debug mode. "
                  "Timings may be affected.")
endif()

# The shell command needs to be run before benchmarks are started.
if(LUAJIT_BENCH_INIT)
  message(STATUS
    "The following command will run before benchmarks: '${LUAJIT_BENCH_INIT}'."
  )
endif()

set(PERF_OUTPUT_DIR ${PROJECT_BINARY_DIR}/perf/output)
file(MAKE_DIRECTORY ${PERF_OUTPUT_DIR})

# List of paths that will be used for each suite.
make_lua_path(LUA_PATH_BENCH_BASE
  PATHS
    # Use of the bench module.
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/?.lua
    # Simple usage with `jit.dump()`, etc.
    ${LUAJIT_SOURCE_DIR}/?.lua
    ${LUAJIT_BINARY_DIR}/?.lua
)

make_lua_path(LUA_CPATH
  PATHS
    # XXX: Some arches may have installed the cjson module here.
    /usr/lib64/lua/5.1/?.so
)

# Produce the pair:
# Target to run for reporting and target to inspect from the
# console, runnable by the CTest.
macro(AddBenchTarget perf_suite)
  file(MAKE_DIRECTORY "${PERF_OUTPUT_DIR}/${perf_suite}/")
  message(STATUS "Add perf suite ${perf_suite}")
  add_custom_target(${perf_suite})
  add_custom_target(${perf_suite}-console
    COMMAND ${CMAKE_CTEST_COMMAND}
      --label-regex ${perf_suite}
      --parallel 1
      --verbose
      --output-on-failure
      --no-tests=error
  )
  add_dependencies(${perf_suite}-console luajit-main)
endmacro()

# Add the bench to the pair of targets created by the call above.
macro(AddBench bench_name bench_path perf_suite LUA_PATH)
  set(bench_title "perf/${perf_suite}/${bench_name}")
  get_filename_component(bench_name_stripped  ${bench_name} NAME_WE)
  set(bench_out_file
    ${PERF_OUTPUT_DIR}/${perf_suite}/${bench_name_stripped}.json
  )
  set(bench_command "${LUAJIT_BENCH_INIT} ${LUAJIT_BINARY} ${bench_path}")
  if(${ARGC} GREATER 4)
    set(input_file ${ARGV4})
    set(bench_command "${bench_command} < ${input_file}")
  endif()
  separate_arguments(bench_command_separated UNIX_COMMAND ${bench_command})
  add_custom_command(
    COMMAND ${CMAKE_COMMAND} -E env
      LUA_PATH="${LUA_PATH}"
      LUA_CPATH="${LUA_CPATH}"
        ${bench_command_separated}
          --benchmark_out_format=json
          --benchmark_out="${bench_out_file}"
    OUTPUT ${bench_out_file}
    DEPENDS luajit-main
    COMMENT
      "Running benchmark ${bench_title} saving results in ${bench_out_file}."
  )
  add_custom_target(${bench_name} DEPENDS ${bench_out_file})
  add_dependencies(${perf_suite} ${bench_name})

  # Report in the console.
  add_test(NAME ${bench_title}
    COMMAND ${SH} -c "${bench_command}"
  )
  set_tests_properties(${bench_title} PROPERTIES
    ENVIRONMENT "LUA_PATH=${LUA_PATH}"
    LABELS ${perf_suite}
    DEPENDS luajit-main
  )
  unset(input_file)
endmacro()

add_subdirectory(LuaJIT-benches)

add_custom_target(${PROJECT_NAME}-perf
  DEPENDS LuaJIT-benches
)

add_custom_target(${PROJECT_NAME}-perf-console
  DEPENDS LuaJIT-benches-console
)

set(PERF_SUMMARY ${PERF_OUTPUT_DIR}/summary.txt)
add_custom_target(${PROJECT_NAME}-perf-aggregate
  BYPRODUCTS ${PERF_SUMMARY}
  COMMENT "Aggregate performance test results into ${PERF_SUMMARY}"
  COMMAND ${CMAKE_COMMAND} -E env
    LUA_CPATH="${LUA_CPATH}"
      ${LUAJIT_BINARY} ${CMAKE_CURRENT_SOURCE_DIR}/helpers/aggregate.lua
        ${PERF_SUMMARY}
        ${PERF_OUTPUT_DIR}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS luajit-main
)
