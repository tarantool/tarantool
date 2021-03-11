#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(20)

box.execute("DROP TABLE IF EXISTS t1")
box.execute("DROP TABLE IF EXISTS t2")

box.execute("CREATE TABLE t1 (s1 INT UNIQUE, s2 INT PRIMARY KEY);")
box.execute("CREATE TABLE t2 (s1 INT UNIQUE, s2 INT PRIMARY KEY);")

box.execute("INSERT INTO t2 VALUES (1,1);")
box.execute("INSERT INTO t1 VALUES (3,3);")

for _, prefix in pairs({"BEFORE", "AFTER"}) do
    box.execute('DROP TRIGGER IF EXISTS t1i')
    box.execute('CREATE TRIGGER t1i '..prefix..' INSERT ON t1 FOR EACH ROW \
                     BEGIN INSERT INTO t2 VALUES (1,1); END')

    test:do_catchsql_test(prefix..'_insert1',
                          'INSERT INTO t1 VALUES(1, 2)',
                          {1, "Duplicate key exists in unique index \"pk_unnamed_T2_2\" in space \"T2\" with old tuple - [1, 1] and new tuple - [1, 1]"})

    test:do_execsql_test(prefix..'_insert1_check1',
                         'SELECT *  FROM t1',
                         {3, 3})

    test:do_execsql_test(prefix..'_insert1_check2',
                         'SELECT *  FROM t2',
                         {1, 1})

    box.execute('DROP TRIGGER IF EXISTS t1u')
    box.execute('CREATE TRIGGER t1u '..prefix..' UPDATE ON t1 FOR EACH ROW \
                     BEGIN INSERT INTO t2 VALUES (1,1); END')

    test:do_catchsql_test(prefix..'_update1',
                          'UPDATE t1 SET s1=1',
                          {1, "Duplicate key exists in unique index \"pk_unnamed_T2_2\" in space \"T2\" with old tuple - [1, 1] and new tuple - [1, 1]"})

    test:do_execsql_test(prefix..'_update1_check1',
                         'SELECT *  FROM t1',
                         {3, 3})

    test:do_execsql_test(prefix..'_insert1_check2',
                         'SELECT *  FROM t2',
                         {1, 1})

    box.execute('DROP TRIGGER IF EXISTS t1ds')
    -- FOR EACH STATEMENT
    box.execute('CREATE TRIGGER t1ds '..prefix..' DELETE ON t1 FOR EACH ROW\
                       BEGIN INSERT INTO t2 VALUES (2,2); \
                             INSERT INTO t2 VALUES (2,2); END')

    test:do_catchsql_test(prefix..'delete1',
                          'DELETE FROM t1;',
                          {1, "Duplicate key exists in unique index \"pk_unnamed_T2_2\" in space \"T2\" with old tuple - [2, 2] and new tuple - [2, 2]"})

    -- Nothing should be inserted due to abort
    test:do_execsql_test('delete1_check1',
                         'SELECT * FROM t2',
                         {1, 1})

    -- Nothing should be deleted
    test:do_execsql_test('delete1_check2',
                         'SELECT * FROM t1',
                         {3, 3})

end

-- Check multi-insert
test:do_catchsql_test('insert2',
                      'INSERT INTO t1 VALUES (5, 6), (6, 7)',
                      {1, "Duplicate key exists in unique index \"pk_unnamed_T2_2\" in space \"T2\" with old tuple - [1, 1] and new tuple - [1, 1]"})
test:do_execsql_test('insert2_check',
                     'SELECT * FROM t1;',
                     {3, 3})

-- Cleanup
box.execute('DROP TRIGGER IF EXISTS t1i')
box.execute('DROP TRIGGER IF EXISTS t1u')
box.execute('DROP TRIGGER IF EXISTS t1ds')
box.execute('DROP TABLE t1')
box.execute('DROP TABLE t2')

test:finish_test()
