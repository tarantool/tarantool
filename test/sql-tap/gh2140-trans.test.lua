#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(10)

box.sql.execute("DROP TABLE IF EXISTS t1")
box.sql.execute("DROP TABLE IF EXISTS t2")

box.sql.execute("CREATE TABLE t1 (s1 int primary key, s2 int);")
box.sql.execute("CREATE TABLE t2 (s1 int primary key, s2 int);")

box.sql.execute("INSERT INTO t1 VALUES (1,1);")
box.sql.execute("INSERT INTO t2 VALUES (1,1);")

test:do_execsql_test('commit1_check',
                     [[START TRANSACTION;
                         INSERT INTO t1 VALUES (2,2);
                       COMMIT;

                       SELECT * FROM t1]],
                     {1, 1, 2, 2})

test:do_execsql_test('rollback1_check',
                     [[START TRANSACTION;
                         INSERT INTO t1 VALUES (3,3);
                       ROLLBACK;

                       SELECT * FROM t1]],
                     {1, 1, 2, 2})

for _, verb in ipairs({'ROLLBACK', 'ABORT'}) do
    box.sql.execute('DELETE FROM t2')
    answer = "Duplicate key exists in unique index 'pk_unnamed_T1_1' in space 'T1'"
    test:do_catchsql_test('insert1_'..verb,
                          [[START TRANSACTION;
                            INSERT INTO t2 VALUES (2, 2);
                            INSERT OR ]]..verb..[[ INTO t1 VALUES (1,1);
                          ]],
                          {1, answer})

    local expect = {}
    if verb == 'ABORT' then
         box.sql.execute('COMMIT')
         expect = {2, 2}
    end
    test:do_execsql_test('insert1_'..verb..'_check',
                         'SELECT * FROM t2', expect)

    box.sql.execute('DELETE FROM t2')
    test:do_catchsql_test('update1_'..verb,
                          [[START TRANSACTION;
                            INSERT INTO t2 VALUES (2, 2);
                            UPDATE OR ]]..verb..[[ t1 SET s1 = 1 WHERE s1 = 2;
                          ]],
                          {1, answer})

    test:do_execsql_test('update1_'..verb..'check',
                         'SELECT * FROM t2', expect)
end

box.sql.execute('COMMIT')
-- Cleanup
box.sql.execute('DROP TABLE t1')
box.sql.execute('DROP TABLE t2')

test:finish_test()
