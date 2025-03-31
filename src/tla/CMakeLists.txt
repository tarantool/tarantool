find_program(TLC tlc)
if (NOT EXISTS ${TLC})
  message(WARNING "TLC is required for running TLA+ tests")
  return()
endif()

set(TEST_SUITE_NAME "tla")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

list(APPEND TLA_LIBRARIES
  ${PROJECT_SOURCE_DIR}/src/tla
  ${PROJECT_SOURCE_DIR}/src/tla/modules
)
list(JOIN TLA_LIBRARIES ":" TLA_LIBRARIES_STR)

list(APPEND specs
  ${CMAKE_CURRENT_SOURCE_DIR}/tarantool.tla
)
foreach(spec_path ${specs})
  get_filename_component(spec_name ${spec_path} NAME)
  # FIXME: By default, GLOB lists directories.
  # Directories are omitted in the result if LIST_DIRECTORIES
  # is set to false. New in version CMake 3.3.
  if(${spec_name} STREQUAL ${TEST_SUITE_NAME})
    continue()
  endif()
  set(spec_title "src/tla/${spec_name}")
  get_filename_component(spec_dir "${spec_path}" DIRECTORY)
  get_filename_component(spec_name "${spec_path}" NAME_WLE)
  add_test(NAME ${spec_title}
           COMMAND ${TLC} ${spec_name}
           WORKING_DIRECTORY ${spec_dir}
  )
  set_tests_properties(${spec_title} PROPERTIES
    ENVIRONMENT "JAVA_OPTS=-DTLA-Library=${TLA_LIBRARIES_STR}"
    LABELS "${TEST_SUITE_NAME};regression;"
  )
endforeach()
