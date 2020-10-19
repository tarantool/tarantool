#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(10)

box.execute("DROP TABLE IF EXISTS t1")
box.execute("DROP TABLE IF EXISTS t2")

box.execute("CREATE TABLE t1 (s0 INT PRIMARY KEY, s1 INT UNIQUE, s2 INT);")
box.execute("CREATE TABLE t2 (s0 INT PRIMARY KEY, s1 INT UNIQUE, s2 INT);")

box.execute("INSERT INTO t1 VALUES (1,1,1);")
box.execute("INSERT INTO t2 VALUES (1,1,1);")

test:do_execsql_test('commit1_check',
                     [[START TRANSACTION;
                         INSERT INTO t1 VALUES (2,2,2);
                       COMMIT;

                       SELECT s1,s2 FROM t1]],
                     {1, 1, 2, 2})

test:do_execsql_test('rollback1_check',
                     [[START TRANSACTION;
                         INSERT INTO t1 VALUES (3,3,3);
                       ROLLBACK;

                       SELECT s1,s2 FROM t1]],
                     {1, 1, 2, 2})

for _, verb in ipairs({'ROLLBACK', 'ABORT'}) do
    box.execute('DELETE FROM t2')
    local answer = "/Duplicate key exists in unique index 'unique_unnamed_T1_2' in space 'T1'/"
    test:do_catchsql_test('insert1_'..verb,
                          [[START TRANSACTION;
                            INSERT INTO t2 VALUES (20, 2, 2);
                            INSERT OR ]]..verb..[[ INTO t1 VALUES (10,1,1);
                          ]],
                          {1, answer})

    local expect = {}
    if verb == 'ABORT' then
         box.execute('COMMIT')
         expect = {20, 2, 2}
    end
    test:do_execsql_test('insert1_'..verb..'_check',
                         'SELECT * FROM t2', expect)

    box.execute('DELETE FROM t2')
    test:do_catchsql_test('update1_'..verb,
                          [[START TRANSACTION;
                            INSERT INTO t2 VALUES (20, 2, 2);
                            UPDATE OR ]]..verb..[[ t1 SET s1 = 1 WHERE s1 = 2;
                          ]],
                          {1, answer})

    test:do_execsql_test('update1_'..verb..'check',
                         'SELECT * FROM t2', expect)
end

box.execute('COMMIT')
-- Cleanup
box.execute('DROP TABLE t1')
box.execute('DROP TABLE t2')

test:finish_test()
