function(SetHwArchString outvar)
  if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(hw_arch "i386")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(hw_arch "x86_64")
  else ()
    message(FATAL_ERROR "Unsupported architecture.")
  endif ()
  set(${outvar} ${hw_arch} PARENT_SCOPE)
endfunction()

# The function sets the given variable in a parent scope to a
# value with path to a path to libclang_rt.fuzzer_no_main [1]
# library. Function raises a fatal message if C compiler is not
# Clang.
#
# $ clang-15 -print-file-name=libclang_rt.fuzzer_no_main-x86_64.a
# $ /usr/lib/llvm-15/lib/clang/15.0.7/lib/linux/libclang_rt.fuzzer_no_main-x86_64.a
#
# 1. https://llvm.org/docs/LibFuzzer.html#using-libfuzzer-as-a-library
function(SetLibFuzzerPath outvar)
  if (NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "C compiler is not a Clang")
  endif ()

  SetHwArchString(HW_ARCH)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(lib_name "libclang_rt.fuzzer_no_main-${HW_ARCH}.a")
  else()
    message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}")
  endif()

  execute_process(COMMAND ${CMAKE_C_COMPILER} "-print-file-name=${lib_name}"
    RESULT_VARIABLE CMD_ERROR
    OUTPUT_VARIABLE CMD_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if (CMD_ERROR)
    message(FATAL_ERROR "${CMD_ERROR}")
  endif()

  set(${outvar} ${CMD_OUTPUT} PARENT_SCOPE)
  message(STATUS "[SetLibFuzzerPath] ${outvar} is ${CMD_OUTPUT}")
endfunction()

# The function find a path to libFuzzer, copy library to a
# directory <build/lf>, unpack library archive and return a CMake
# list with paths to libFuzzer's object files.
function(SetLibFuzzerObjFiles outvar)
  set(LibFuzzerDir ${PROJECT_BINARY_DIR}/lf)
  SetLibFuzzerPath(LibFuzzerPath)
  file(MAKE_DIRECTORY ${LibFuzzerDir})
  get_filename_component(LibFuzzerFilename ${LibFuzzerPath} NAME)
  file(COPY_FILE ${LibFuzzerPath}
    ${LibFuzzerDir}/${LibFuzzerFilename} RESULT COPY_RES)
  if (NOT COPY_RES EQUAL 0)
    message(FATAL_ERROR "Failed to copy ${LibFuzzerPath} to ${LibFuzzerDir}")
  endif()
  execute_process(COMMAND ${CMAKE_AR} x ${LibFuzzerPath}
    RESULT_VARIABLE CMD_ERROR
    OUTPUT_VARIABLE CMD_OUTPUT
    WORKING_DIRECTORY ${LibFuzzerDir}
  )
  if (CMD_ERROR)
    message(FATAL_ERROR "${CMD_ERROR}")
  endif()
  file(GLOB LibFuzzerObjectFiles ${LibFuzzerDir}/*.o)
  set(${outvar} ${LibFuzzerObjectFiles} PARENT_SCOPE)
endfunction()
