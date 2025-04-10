find_program(TLC tlc)
if (NOT EXISTS ${TLC})
  message(WARNING "TLC is required for running TLA+ tests")
  return()
endif()

set(TEST_SUITE_NAME "tla")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

list(APPEND TLA_LIBRARIES
  ${PROJECT_SOURCE_DIR}/proofs/tla/src/modules
  ${PROJECT_SOURCE_DIR}/proofs/tla/src/
)
list(JOIN TLA_LIBRARIES ":" TLA_LIBRARIES_STR)

file(GLOB_RECURSE tests
  ${CMAKE_CURRENT_SOURCE_DIR}/test/*.tla
)

# Remove files that match *_TTrace_*.tla. These are remainings of the runs.
foreach(test ${tests})
  if(test MATCHES ".*_TTrace_.*\\.tla$")
    list(REMOVE_ITEM tests ${test})
  endif()
endforeach()

foreach(test_path ${tests})
  get_filename_component(test_name ${test_path} NAME)
  # FIXME: By default, GLOB lists directories.
  # Directories are omitted in the result if LIST_DIRECTORIES
  # is set to false. New in version CMake 3.3.
  if(${test_name} STREQUAL ${TEST_SUITE_NAME})
    continue()
  endif()
  set(test_title "test/${TEST_SUITE_NAME}/${test_name}")
  get_filename_component(test_dir "${test_path}" DIRECTORY)
  get_filename_component(test_name "${test_path}" NAME_WLE)
  add_test(NAME ${test_title}
           COMMAND ${TLC} -workers auto ${test_name}
           WORKING_DIRECTORY ${test_dir}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "JAVA_OPTS=-DTLA-Library=${TLA_LIBRARIES_STR}"
    LABELS "${TEST_SUITE_NAME};regression;"
  )
endforeach()
