find_program(CODESPELL codespell)

set(CODESPELL_WHITELIST
  ${PROJECT_SOURCE_DIR}/.flake8rc
  ${PROJECT_SOURCE_DIR}/.github
  ${PROJECT_SOURCE_DIR}/.luacheckrc
  ${PROJECT_SOURCE_DIR}/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/cmake
  ${PROJECT_SOURCE_DIR}/src/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/src/lib_misc.c
  ${PROJECT_SOURCE_DIR}/src/lj_mapi.c
  ${PROJECT_SOURCE_DIR}/src/lj_memprof.c
  ${PROJECT_SOURCE_DIR}/src/lj_memprof.h
  ${PROJECT_SOURCE_DIR}/src/lj_symtab.c
  ${PROJECT_SOURCE_DIR}/src/lj_symtab.h
  ${PROJECT_SOURCE_DIR}/src/lj_sysprof.c
  ${PROJECT_SOURCE_DIR}/src/lj_sysprof.h
  ${PROJECT_SOURCE_DIR}/src/lj_utils.h
  ${PROJECT_SOURCE_DIR}/src/lj_utils_leb128.c
  ${PROJECT_SOURCE_DIR}/src/lj_wbuf.c
  ${PROJECT_SOURCE_DIR}/src/lj_wbuf.h
  ${PROJECT_SOURCE_DIR}/src/lmisclib.h
  ${PROJECT_SOURCE_DIR}/src/luajit-gdb.py
  ${PROJECT_SOURCE_DIR}/src/luajit_lldb.py
  ${PROJECT_SOURCE_DIR}/test/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/test/LuaJIT-tests/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/test/PUC-Rio-Lua-5.1-tests/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/test/PUC-Rio-Lua-5.1-tests/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/test/PUC-Rio-Lua-5.1-tests/libs/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/test/lua-Harness-tests/CMakeLists.txt
  ${PROJECT_SOURCE_DIR}/test/tarantool-c-tests
  ${PROJECT_SOURCE_DIR}/test/tarantool-tests
  ${PROJECT_SOURCE_DIR}/tools
)

set(IGNORE_WORDS ${PROJECT_SOURCE_DIR}/.codespell-ignore-words.txt)

add_custom_target(${PROJECT_NAME}-codespell)
if(CODESPELL)
  add_custom_command(TARGET ${PROJECT_NAME}-codespell
    COMMENT "Running codespell"
    COMMAND
      ${CODESPELL}
        --ignore-words ${IGNORE_WORDS}
        --skip ${IGNORE_WORDS}
        --check-filenames
        ${CODESPELL_WHITELIST}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
else()
  set(STR1 "codespell is not found,")
  set(STR2 "so ${PROJECT_NAME}-codespell target is dummy")
  string(CONCAT WARN_MSG "${STR1} ${STR2}")
  add_custom_command(TARGET ${PROJECT_NAME}-codespell
    COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red ${WARN_MSG}
    COMMENT ${WARN_MSG}
  )
endif()
