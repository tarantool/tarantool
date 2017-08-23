option(SQL_MAINTAINER_MODE "Regenerate parser and opcodes for SQL compiler" OFF)

# SQLITE_DEBUG == 1 is needed by all tracing capabilities
# which are heavily used in tests.
# if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
# set(SQLITE_DEBUG 1)
# endif()

add_definitions(-Werror -Wall)

set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${SRCDIR})

add_definitions(-DSQLITE_MAX_WORKER_THREADS=0)
add_definitions(-DSQLITE_OMIT_VACUUM=1)
add_definitions(-DTHREADSAFE=0)

set(TEST_DEFINITIONS
    SQLITE_DEBUG=1
    SQLITE_NO_SYNC=1
    TCLSH=1
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
    auth.c
    bitvec.c
    btmutex.c
    btree.c
    build.c
    callback.c
    complete.c
    date.c
    dbstat.c
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
    mem0.c
    mem1.c
    mem2.c
    mem3.c
    mem5.c
    memjournal.c
    mutex.c
    mutex_noop.c
    mutex_unix.c
    mutex_w32.c
    notify.c
    os.c
    os_unix.c
    os_win.c
    pager.c
    pcache.c
    pcache1.c
    pragma.c
    prepare.c
    printf.c
    random.c
    resolve.c
    rowset.c
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
    vdbeblob.c
    vdbemem.c
    vdbesort.c
    vdbetrace.c
    vtab.c
    walker.c
    where.c
    wherecode.c
    whereexpr.c
)
set_target_properties(sql PROPERTIES COMPILE_DEFINITIONS
    "${TEST_DEFINITIONS}")

if(SQL_MAINTAINER_MODE)
    message(STATUS "SQL maintainer mode is enabled.")
    find_program(TCLSH tclsh)

    add_custom_target(generate_sql_files DEPENDS
        parse.h
        keywordhash.h
        parse.y
        parse.c
        opcodes.c)

    add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/keywordhash.h
        COMMAND ${CMAKE_BINARY_DIR}/extra/mkkeywordhash > keywordhash.h.tmp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different keywordhash.h.tmp keywordhash.h
        COMMAND ${CMAKE_COMMAND} -E remove keywordhash.h.tmp
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS mkkeywordhash)

    add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/parse.h ${CMAKE_CURRENT_SOURCE_DIR}/parse.c
        COMMAND ${CMAKE_BINARY_DIR}/extra/lemon -DSQLITE_OMIT_VACUUM -T${CMAKE_SOURCE_DIR}/extra/lempar.c parse.y
        COMMAND ${CMAKE_COMMAND} -E copy parse.h parse.h.tmp
        COMMAND ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/addopcodes.tcl parse.h.tmp > parse.h
        COMMAND ${CMAKE_COMMAND} -E remove parse.h.tmp parse.out
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS lemon parse.y)

    add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/opcodes.h
        COMMAND cat parse.h vdbe.c | ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/mkopcodeh.tcl > opcodes.h
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS vdbe.c ${CMAKE_SOURCE_DIR}/extra/mkopcodeh.tcl parse.h)

    add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/opcodes.c
        COMMAND ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/mkopcodec.tcl opcodes.h > opcodes.c
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS vdbe.c ${CMAKE_SOURCE_DIR}/extra/mkopcodec.tcl parse.h opcodes.h)

    add_dependencies(sql generate_sql_files)
endif()

if (APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined suppress -flat_namespace")
endif(APPLE)
