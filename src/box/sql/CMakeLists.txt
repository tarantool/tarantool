if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DSQLITE_DEBUG=1)
  add_definitions(-DSQLITE_ENABLE_SELECTTRACE)
  add_definitions(-DSQLITE_ENABLE_WHERETRACE)

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
endif()

set(EXT_SRC_DIR ${CMAKE_SOURCE_DIR}/extra)
set(EXT_BIN_DIR ${CMAKE_BINARY_DIR}/extra)
set(SQL_SRC_DIR ${CMAKE_SOURCE_DIR}/src/box/sql)
set(SQL_BIN_DIR ${CMAKE_BINARY_DIR}/src/box/sql)

include_directories(${SQL_SRC_DIR})
include_directories(${SQL_BIN_DIR})

add_definitions(-DSQLITE_MAX_WORKER_THREADS=0)
add_definitions(-DSQLITE_DEFAULT_FOREIGN_KEYS=1)
add_definitions(-DSQLITE_OMIT_AUTOMATIC_INDEX)

set(TEST_DEFINITIONS
    SQLITE_NO_SYNC=1
    SQLITE_TEST=1
    SQLITE_PRIVATE=
    SQLITE_CORE=1
)

add_library(sql STATIC
    # Generated files
    opcodes.c
    parse.c
    alter.c
    analyze.c
    attach.c
    cursor.c
    build.c
    callback.c
    complete.c
    date.c
    delete.c
    expr.c
    fault.c
    fkey.c
    func.c
    global.c
    hash.c
    insert.c
    legacy.c
    main.c
    malloc.c
    os.c
    os_unix.c
    pragma.c
    prepare.c
    printf.c
    random.c
    resolve.c
    select.c
    status.c
    table.c
    tokenize.c
    treeview.c
    trigger.c
    utf.c
    update.c
    util.c
    vdbe.c
    vdbeapi.c
    vdbeaux.c
    vdbemem.c
    vdbesort.c
    vdbetrace.c
    walker.c
    where.c
    wherecode.c
    whereexpr.c
)
set_target_properties(sql PROPERTIES COMPILE_DEFINITIONS
    "${TEST_DEFINITIONS}")

add_custom_target(generate_sql_files DEPENDS
    parse.h
    keywordhash.h
    parse.y
    parse.c
    opcodes.c)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/keywordhash.h
    COMMAND ${EXT_BIN_DIR}/mkkeywordhash > keywordhash.h.tmp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different keywordhash.h.tmp keywordhash.h
    COMMAND ${CMAKE_COMMAND} -E remove keywordhash.h.tmp
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS mkkeywordhash)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/parse.h ${SQL_BIN_DIR}/parse.c
    COMMAND ${EXT_BIN_DIR}/lemon -T${EXT_SRC_DIR}/lempar.c ${SQL_SRC_DIR}/parse.y
    COMMAND ${CMAKE_COMMAND} -E copy parse.h parse.h.tmp
    COMMAND ${EXT_SRC_DIR}/addopcodes.sh parse.h.tmp > parse.h
    COMMAND ${CMAKE_COMMAND} -E remove parse.h.tmp parse.out
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS lemon ${SQL_SRC_DIR}/parse.y)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/opcodes.h
    COMMAND cat parse.h ${SQL_SRC_DIR}/vdbe.c | ${EXT_SRC_DIR}/mkopcodeh.sh > opcodes.h
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS ${SQL_SRC_DIR}/vdbe.c ${EXT_SRC_DIR}/mkopcodeh.sh ${SQL_BIN_DIR}/parse.h)

add_custom_command(OUTPUT ${SQL_BIN_DIR}/opcodes.c
    COMMAND ${EXT_SRC_DIR}/mkopcodec.sh opcodes.h > opcodes.c
    WORKING_DIRECTORY "${SQL_BIN_DIR}"
    DEPENDS ${SQL_SRC_DIR}/vdbe.c ${EXT_SRC_DIR}/mkopcodec.sh ${SQL_BIN_DIR}/parse.h
        ${SQL_BIN_DIR}/opcodes.h)

add_dependencies(sql generate_sql_files)

if (APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined suppress -flat_namespace")
endif(APPLE)
