# CheckUnwindTables provides a convenient way to define whether
# the target toolchain generates unwind tables.
#
# This function implements black voodoo magic provided by Mike
# Pall in scope of e131936133c58de4426c595db2341caf5a1665b5
# ("Cleanup and enable external unwinding for more platforms.")
# via CMake.
#
# Example usage:
#
#   # Find out whether the target toolchain always generates
#   # unwind tables.
#   CheckUnwindTables(HAVE_UNWIND_TABLES)
#   if(HAVE_UNWIND_TABLES)
#     AppendFlags(TARGET_C_FLAGS -DLUAJIT_UNWIND_EXTERNAL)
#   endif()

function(CheckUnwindTables status)
  set(_CHECK_UNWIND_TABLES_RESULT FALSE)

  # 1. Build the command compiling the simple object file using
  #    the target toolchain.
  # XXX: CMake helper <try_compile> can't be used, since there is
  # no executable file as a result, but only an object file (in
  # other words, there is no <main> function in C source file).
  set(_TEST_UNWIND_OBJECT "${CMAKE_BINARY_DIR}/test-unwind-tables.o")
  string(CONCAT _TEST_UNWIND_SOURCE
    "extern void b(void);"
    "int a(void) {"
      "b();"
      "return 0;"
    "}"
  )
  string(CONCAT _TEST_UNWIND_COMPILE
    "echo '${_TEST_UNWIND_SOURCE}'"
    "|"
    "${CMAKE_C_COMPILER} -c -x c - -o ${_TEST_UNWIND_OBJECT}"
  )

  # 2. Use <grep> to find either .eh_frame (for ELF) or
  #    __unwind_info (for Mach-O) entries in the compiled object
  #    file. After the several attempts in scope of the following
  #    commits:
  #    * e131936133c58de4426c595db2341caf5a1665b5 ("Cleanup and
  #      enable external unwinding for more platforms.")
  #    * d4a554d6ee1507f7313641b26ed09bf1b518fa1f ("OSX: Fix build
  #      by hardcoding external frame unwinding.")
  #    * b9d523965b3f55b19345a1ed1ebc92e431747ce1 ("BSD: Fix build
  #      with BSD grep.")
  #    * 66563bdab0c7acf3cd61dc6cfcca36275951d084 ("Fix build with
  #      busybox grep.")
  #    Mike came up with the solution to use both alternatives:
  #    `grep -qa' as the major one and `grep -qU' as a fallback for
  #    BSD platforms.
  set(_TEST_GREP_GNU "grep -qa")
  set(_TEST_GREP_BSD "grep -qU")
  set(_TEST_GREP_PATTERN "-e eh_frame -e __unwind_info")
  string(CONCAT _TEST_UNWIND_CHECK
    # XXX: Mind the space after the opening brace. The space is
    # vital since { is a *reserved word* (i.e. the command built
    # into the shell). For more info see the following link:
    # https://www.gnu.org/software/bash/manual/html_node/Reserved-Words.html.
    "{ "
    "${_TEST_GREP_GNU} ${_TEST_GREP_PATTERN} ${_TEST_UNWIND_OBJECT}"
    "||"
    "${_TEST_GREP_BSD} ${_TEST_GREP_PATTERN} ${_TEST_UNWIND_OBJECT}"
    # XXX: Mind the semicolon prior to the closing brace. The
    # semicolon is vital due to we are executing the list of shell
    # commands that has to be terminated by one of ';', '&', or a
    # newline. Considering it will be executed synchronously via
    # <execute_process>, only the first option fits here. For more
    # info see the following link:
    # https://www.gnu.org/software/bash/manual/html_node/Lists.html
    # XXX: Considering the preceding semicolon, there is no need to
    # separate } command with the additional whitespace.
    ";}"
  )

  # 3. Use step 1 and step 2 to check whether target toolchain
  #    always generates unwind tables.
  # XXX: There is no need in "echo E" command at the end, since
  # we can check $? of the command by <RESULT_VARIABLE> value.
  # Fun fact: there is $(.SHELLSTATUS) variable in GNU Make, but
  # it can't be used in <ifeq>/<ifneq> conditions, so we can't get
  # rid of "echo E" in the original Makefile machinery.
  execute_process(
    COMMAND /bin/sh -c "${_TEST_UNWIND_COMPILE} && ${_TEST_UNWIND_CHECK}"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE _TEST_UNWIND_RC
    ERROR_QUIET
    OUTPUT_QUIET
  )

  if(_TEST_UNWIND_RC EQUAL 0)
    set(_CHECK_UNWIND_TABLES_RESULT TRUE)
  endif()

  # Remove generated object file.
  file(REMOVE ${_TEST_UNWIND_OBJECT})

  set(${status} ${_CHECK_UNWIND_TABLES_RESULT} PARENT_SCOPE)
  # XXX: Unset the internal variable to not spoil CMake cache.
  # Study the case in CheckIPOSupported.cmake, that affected this
  # module: https://gitlab.kitware.com/cmake/cmake/-/commit/4b82977
  unset(_CHECK_UNWIND_TABLES_RESULT)
  unset(_TEST_UNWIND_RC)
  unset(_TEST_UNWIND_CHECK)
  unset(_TEST_GREP_PATTERN)
  unset(_TEST_GREP_BSD)
  unset(_TEST_GREP_GNU)
  unset(_TEST_UNWIND_COMPILE)
  unset(_TEST_UNWIND_SOURCE)
  unset(_TEST_UNWIND_OBJECT)
endfunction()
