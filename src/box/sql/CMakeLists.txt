if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    set(SQLITE_DEBUG 1)
endif()

add_definitions(-Wno-pointer-sign -Wno-error)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")

set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(OUTPUT keywordhash.h
    COMMAND ${CMAKE_BINARY_DIR}/extra/mkkeywordhash > keywordhash.h
    DEPENDS mkkeywordhash)

add_custom_command(OUTPUT parse.h parse.c
    COMMAND ${CMAKE_BINARY_DIR}/extra/lemon -DSQLITE_OMIT_VACUUM -T${CMAKE_SOURCE_DIR}/extra/lempar.c ${SRCDIR}/parse.y
    COMMAND mv -f parse.h parse.h.tmp
    COMMAND ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/addopcodes.tcl parse.h.tmp > parse.h
    COMMAND rm -f parse.h.tmp parse.out
    DEPENDS lemon
)

add_custom_command(OUTPUT opcodes.h
    DEPENDS vdbe.c ${CMAKE_SOURCE_DIR}/extra/mkopcodeh.tcl parse.h
    COMMAND cat parse.h ${SRCDIR}/vdbe.c | ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/mkopcodeh.tcl > opcodes.h
)

add_custom_command(OUTPUT opcodes.c
    DEPENDS vdbe.c ${CMAKE_SOURCE_DIR}/extra/mkopcodec.tcl parse.h opcodes.h
    COMMAND ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/mkopcodec.tcl opcodes.h > opcodes.c
)

add_custom_command(OUTPUT sqlite3.h
    DEPENDS sqlite.h.in
    COMMAND ${TCLSH} ${CMAKE_SOURCE_DIR}/extra/mksqlite3h.tcl ${SRCDIR} > sqlite3.h
)

add_definitions(-D_HAVE_SQLITE_CONFIG_H=1)
add_definitions(-DSQLITE_MAX_WORKER_THREADS=0)
add_definitions(-DSQLITE_OMIT_VACUUM=1)

add_custom_target(generate_sql_files DEPENDS
    parse.h
    keywordhash.h
    parse.h
    parse.c
    opcodes.c
    sqlite3.h)

set(TEST_DEFINITIONS
    SQLITE_NO_SYNC=1
    TCLSH=1
    SQLITE_TEST=1
    SQLITE_PRIVATE=
    SQLITE_CORE=1
)

add_library(sql
    # Generated files
    opcodes.c
    parse.c

    alter.c
    analyze.c
    attach.c
    auth.c
    backup.c
    bitvec.c
    btmutex.c
    btree.c
    build.c
    callback.c
    complete.c
    ctime.c
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
    loadext.c
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
    wal.c
    walker.c
    where.c
    wherecode.c
    whereexpr.c
)
add_dependencies(sql generate_sql_files)
set_target_properties(sql PROPERTIES COMPILE_DEFINITIONS
    "${TEST_DEFINITIONS}")

if (APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined suppress -flat_namespace")
endif(APPLE)
