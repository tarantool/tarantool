# Building supplementary materials for LuaJIT.

set(LUAJIT_PC_PREFIX ${CMAKE_INSTALL_PREFIX})
if(CMAKE_LIBRARY_ARCHITECTURE)
  set(LUAJIT_PC_MULTILIB "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
else()
  set(LUAJIT_PC_MULTILIB "lib")
endif()

configure_file(luajit.pc.in luajit.pc @ONLY ESCAPE_QUOTES)

install(FILES
    luajit.1
  DESTINATION share/man/man1
  PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ
  COMPONENT luajit
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/luajit.pc
  DESTINATION lib/pkgconfig
  PERMISSIONS
    OWNER_READ OWNER_WRITE
    GROUP_READ
    WORLD_READ
  COMPONENT luajit
)
