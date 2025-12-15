# Test suite that has been added from PUC-Rio Lua 5.1 test archive
# in scope of https://github.com/tarantool/tarantool/issues/5845.

# Build additional C libraries for tests.
AddTestLib(lib1 lib1.c)
AddTestLib(lib11 lib1.c lib11.c)
AddTestLib(lib2 lib2.c)
AddTestLib(lib21 lib2.c lib21.c)

# Create the exact copy of the lib2 library for tests
# in <attrib.lua>.
set(LIB2ORIG "${CMAKE_CURRENT_BINARY_DIR}/lib2${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(LIB2COPY "${CMAKE_CURRENT_BINARY_DIR}/-lib2${CMAKE_SHARED_LIBRARY_SUFFIX}")
add_custom_command(
  OUTPUT ${LIB2COPY}
  COMMENT "Copying lib2 to -lib2 for PUC-Rio Lua 5.1 tests"
  COMMAND ${CMAKE_COMMAND} -E copy ${LIB2ORIG} ${LIB2COPY}
  DEPENDS ${TESTLIBS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
list(APPEND TESTLIBS ${LIB2COPY})

# The original tarball contains subdirectory "libs" with an empty
# subdirectory "libs/P1", to be used by tests.
# Instead of tracking empty directory with some anchor-file for
# git, create this directory via CMake.
add_custom_target(PUC-Rio-Lua-5.1-tests-prepare DEPENDS ${TESTLIBS})
add_custom_command(TARGET PUC-Rio-Lua-5.1-tests-prepare
  COMMENT "Create directory for PUC-Rio Lua 5.1 tests"
  COMMAND ${CMAKE_COMMAND} -E make_directory P1
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# vim: expandtab tabstop=2 shiftwidth=2
