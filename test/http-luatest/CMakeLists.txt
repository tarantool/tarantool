set(TEST_SUITE_NAME "http-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};regression;"
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUATEST_LUA_PATH}"
  "VARDIR=${VARDIR}"
)

file(GLOB tests ${CMAKE_CURRENT_SOURCE_DIR} *_test.lua)
foreach(test_path ${tests})
  get_filename_component(test_name ${test_path} NAME)
  if(${test_name} STREQUAL ${TEST_SUITE_NAME})
    continue()
  endif()
  add_luatest_test(${TEST_SUITE_NAME} ${test_name} "${TEST_ENV}" TRUE)
endforeach()
