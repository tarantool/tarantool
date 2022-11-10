## feature/build

* Now the compiler info displayed in `tarantool.build.compiler` and
  `tarantool --version` shows the ID and the version of the compiler
  that was used to build Tarantool. The output has the format
  `${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}`, for example,
  `Clang-14.0.0.14000029` (gh-7888).
