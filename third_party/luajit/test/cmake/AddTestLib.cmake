# Add a test library to build and add it to the `TESTLIBS`
# variable.
macro(AddTestLib lib sources)
  add_library(${lib} SHARED EXCLUDE_FROM_ALL ${sources})
  target_include_directories(${lib} PRIVATE
    ${LUAJIT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_target_properties(${lib} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PREFIX ""
  )

  # XXX: This change affects the current CMake variable scope, so
  # a user shouldn't use it in a top-level scope.
  # The dynamic libraries are loaded with LuaJIT binary and use
  # symbols from it. So it is totally OK to have unresolved
  # symbols at build time.
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set_target_properties(${lib} PROPERTIES
      LINK_FLAGS "-undefined dynamic_lookup"
    )
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # FIXME: Unfortunately, there is no another way to suppress
    # this linker option, so just strip it out from the flags.
    string(REPLACE "-Wl,--no-undefined" ""
      CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}"
    )
  endif()
  list(APPEND TESTLIBS ${lib})
endmacro()
