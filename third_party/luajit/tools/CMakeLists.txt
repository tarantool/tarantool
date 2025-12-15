# Building tools for developing with LuaJIT.
# Major portions taken verbatim or adapted from the uJIT.
# Copyright (C) 2020-2021 LuaVela Authors.
# Copyright (C) 2015-2020 IPONWEB Ltd.

set(LUAJIT_TOOLS_DEPS)

if(LUAJIT_DISABLE_MEMPROF)
  message(STATUS "LuaJIT memory profiler support is disabled")
else()
  add_custom_target(tools-parse-memprof EXCLUDE_FROM_ALL DEPENDS
    memprof/humanize.lua
    memprof/parse.lua
    memprof/process.lua
    memprof.lua
    utils/avl.lua
    utils/bufread.lua
    utils/evread.lua
    utils/symtab.lua
  )
  list(APPEND LUAJIT_TOOLS_DEPS tools-parse-memprof)

  install(FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/memprof/humanize.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/memprof/parse.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/memprof/process.lua
    DESTINATION ${LUAJIT_DATAROOTDIR}/memprof
    PERMISSIONS
      OWNER_READ OWNER_WRITE
      GROUP_READ
      WORLD_READ
    COMPONENT tools-parse-memprof
  )
  install(FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/avl.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/bufread.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/evread.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/symtab.lua
    DESTINATION ${LUAJIT_DATAROOTDIR}/utils
    PERMISSIONS
      OWNER_READ OWNER_WRITE
      GROUP_READ
      WORLD_READ
    COMPONENT tools-parse-memprof
  )
  install(FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/memprof.lua
    DESTINATION ${LUAJIT_DATAROOTDIR}
    PERMISSIONS
      OWNER_READ OWNER_WRITE
      GROUP_READ
      WORLD_READ
    COMPONENT tools-parse-memprof
  )
endif()



if(LUAJIT_DISABLE_SYSPROF)
  message(STATUS "LuaJIT system profiler support is disabled")
else()
  add_custom_target(tools-parse-sysprof EXCLUDE_FROM_ALL DEPENDS
    sysprof/parse.lua
    sysprof.lua
    utils/avl.lua
    utils/bufread.lua
    utils/evread.lua
    utils/symtab.lua
  )
  list(APPEND LUAJIT_TOOLS_DEPS tools-parse-sysprof)

  install(FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/sysprof/parse.lua
    DESTINATION ${LUAJIT_DATAROOTDIR}/sysprof
    PERMISSIONS
      OWNER_READ OWNER_WRITE
      GROUP_READ
      WORLD_READ
    COMPONENT tools-parse-sysprof
  )
  install(FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/avl.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/bufread.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/evread.lua
      ${CMAKE_CURRENT_SOURCE_DIR}/utils/symtab.lua
    DESTINATION ${LUAJIT_DATAROOTDIR}/utils
    PERMISSIONS
      OWNER_READ OWNER_WRITE
      GROUP_READ
      WORLD_READ
    COMPONENT tools-parse-sysprof
  )
  install(FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/sysprof.lua
    DESTINATION ${LUAJIT_DATAROOTDIR}
    PERMISSIONS
      OWNER_READ OWNER_WRITE
      GROUP_READ
      WORLD_READ
    COMPONENT tools-parse-sysprof
  )
endif()

add_custom_target(LuaJIT-tools DEPENDS ${LUAJIT_TOOLS_DEPS})
