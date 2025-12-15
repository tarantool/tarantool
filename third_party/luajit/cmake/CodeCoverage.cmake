find_program(GCOVR gcovr)
find_program(GCOV gcov)

set(COVERAGE_DIR "${PROJECT_BINARY_DIR}/coverage")
set(COVERAGE_HTML_REPORT "${COVERAGE_DIR}/luajit.html")
set(COVERAGE_XML_REPORT "${COVERAGE_DIR}/luajit.xml")

if(NOT GCOVR OR NOT GCOV)
  set(MSG "LuaJIT-coverage is a dummy target")
  add_custom_target(${PROJECT_NAME}-coverage
    COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red ${MSG}
  )
  message(WARNING "Either `gcovr' or `gcov` not found, "
                  "so ${PROJECT_NAME}-coverage target is dummy.")
  return()
endif()

file(MAKE_DIRECTORY ${COVERAGE_DIR})
add_custom_target(${PROJECT_NAME}-coverage)
add_custom_command(TARGET ${PROJECT_NAME}-coverage
  COMMENT "Building coverage report"
  COMMAND
    ${GCOVR}
      # See https://gcovr.com/en/stable/guide/configuration.html
      --root ${PROJECT_SOURCE_DIR}
      --object-directory ${PROJECT_BINARY_DIR}
      --filter ${PROJECT_SOURCE_DIR}/src
      # Exclude DynASM files, that contain a low-level VM code for CPUs.
      --exclude ".*\.dasc"
      # Exclude buildvm source code, it's the project's infrastructure.
      --exclude ".*/host/"
      --print-summary
      --output ${COVERAGE_HTML_REPORT}
      --cobertura ${COVERAGE_XML_REPORT}
      --html
      --html-title "Tarantool LuaJIT Code Coverage Report"
      --html-details
      --sort-percentage
      --branches
      --decisions
      -j ${CMAKE_BUILD_PARALLEL_LEVEL}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

message(STATUS "Code coverage HTML report: ${COVERAGE_HTML_REPORT}")
message(STATUS "Code coverage XML report: ${COVERAGE_XML_REPORT}")
