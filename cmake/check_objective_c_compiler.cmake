#
# Check if CXX compiler can compile ObjectiveC sources.
#  The code was borrowed from CMakeTestCXXCompiler.cmake
#
IF(NOT CMAKE_CXX_OBJECTIVEC_COMPILER_WORKS)
  MESSAGE(STATUS "Check for working ObjectiveC compiler: ${CMAKE_CXX_COMPILER}...")
  FILE(WRITE ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/testCXXObjectiveCCompiler.m 
    "int main(){return 0;}\n")
  TRY_COMPILE(CMAKE_CXX_OBJECTIVEC_COMPILER_WORKS ${CMAKE_BINARY_DIR} 
    ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/testCXXObjectiveCCompiler.m
    OUTPUT_VARIABLE OUTPUT)
  SET(CXX_OBJECTIVEC_TEST_WAS_RUN 1)
ENDIF(NOT CMAKE_CXX_OBJECTIVEC_COMPILER_WORKS)
IF(NOT CMAKE_CXX_OBJECTIVEC_COMPILER_WORKS)
  MESSAGE(STATUS "Check for working ObjectiveC compiler: ${CMAKE_CXX_COMPILER} -- broken")
  FILE(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
    "Determining if the ObjectiveC compiler works failed with "
    "the following output:\n${OUTPUT}\n\n")
  MESSAGE(FATAL_ERROR "Compiler \"${CMAKE_CXX_COMPILER}\" "
    "is not able to compile a simple ObjectiveC test program."
    " Please check ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log "
    "to see how it fails. "
    "CMake will not be able to correctly generate this project.")
ELSE(NOT CMAKE_CXX_OBJECTIVEC_COMPILER_WORKS)
  IF(CXX_OBJECTIVEC_TEST_WAS_RUN)
    MESSAGE(STATUS "Check for working ObjectiveC compiler: ${CMAKE_CXX_COMPILER} -- works")
    FILE(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the (ObjectiveC) compiler works passed with "
      "the following output:\n${OUTPUT}\n\n")
  ENDIF(CXX_OBJECTIVEC_TEST_WAS_RUN)
  SET(CMAKE_CXX_OBJETIVEC_COMPILER_WORKS 1 CACHE INTERNAL "")
ENDIF(NOT CMAKE_CXX_OBJECTIVEC_COMPILER_WORKS)
