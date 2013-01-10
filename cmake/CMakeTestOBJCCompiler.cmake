#
# Check if C compiler can compile ObjectiveC sources.
# The code was borrowed from CMakeTestCXXCompiler.cmake
#

if(CMAKE_OBJC_COMPILER_FORCED OR CMAKE_C_COMPILER_FORCED)
  # The compiler configuration was forced by the user.
  # Assume the user has configured all compiler information.
  set(CMAKE_OBJC_COMPILER_WORKS TRUE)
  return()
endif()

if(NOT CMAKE_OBJC_COMPILER_WORKS)
  message(STATUS "Check for working ObjectiveC compiler: ${CMAKE_C_COMPILER}")
  file(WRITE ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/testOBJCCompiler.m
    "#if !defined(__OBJC__)\n"
    "# error \"The CMAKE_C_COMPILER doesn't support ObjectiveC\"\n"
    "#endif\n"
    "int main(){return 0;}\n")
  try_compile(CMAKE_OBJC_COMPILER_WORKS ${CMAKE_BINARY_DIR} 
    ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/testOBJCCompiler.m
    OUTPUT_VARIABLE __CMAKE_OBJC_COMPILER_OUTPUT)
  # Move result from cache to normal variable.
  set(CMAKE_OBJC_COMPILER_WORKS ${CMAKE_OBJC_COMPILER_WORKS})
  set(OBJC_TEST_WAS_RUN 1)
endif()

if(NOT CMAKE_OBJC_COMPILER_WORKS)
  message(STATUS "Check for working ObjectiveC compiler: "
    "${CMAKE_C_COMPILER}" " -- broken")
  file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
    "Determining if the ObjectiveC compiler works failed with "
    "the following output:\n${__CMAKE_OBJC_COMPILER_OUTPUT}\n\n")
  message(FATAL_ERROR "The ObjectiveC compiler \"${CMAKE_C_COMPILER}\" "
    "is not able to compile a simple ObjectiveC test program.\nIt fails "
    "with the following output:\n ${__CMAKE_OBJC_COMPILER_OUTPUT}\n\n"
    "CMake will not be able to correctly generate this project.")
else()
  if(OBJC_TEST_WAS_RUN)
    message(STATUS "Check for working ObjectiveC compiler: "
        "${CMAKE_C_COMPILER}" " -- works")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the ObjectiveC compiler works passed with "
      "the following output:\n${__CMAKE_OBJC_COMPILER_OUTPUT}\n\n")
  endif()
endif()

unset(__CMAKE_OBJC_COMPILER_OUTPUT)
