# The function sets the given variable in a parent scope to a
# number to CMAKE_BUILD_PARALLEL_LEVEL environment variable if it
# is set. Otherwise the number of CPU cores is get via
# ProcessorCount CMake builtin module. If the ProcessorCount fails
# to determine the number of cores (i.e. yields 0), the given
# variable defaults to 1.

function(SetBuildParallelLevel outvar)
  set(JOBS $ENV{CMAKE_BUILD_PARALLEL_LEVEL})
  if(NOT JOBS)
    set(JOBS 1)
    include(ProcessorCount)
    ProcessorCount(NPROC)
    if(NOT NPROC EQUAL 0)
      set(JOBS ${NPROC})
    endif()
  endif()
  set(${outvar} ${JOBS} PARENT_SCOPE)
  message(STATUS "[SetBuildParallelLevel] ${outvar} is ${JOBS}")
endfunction()
