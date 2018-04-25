#!/usr/bin/env tarantool

-- gh-3350, gh-2859

test = require("sqltester")
test:plan(4)

local function lindex(str, pos)
    return str:sub(pos+1, pos+1)
end

local function int_to_char(i)
    local res = ''
    local char = 'abcdefghij'
    local divs = {1000, 100, 10, 1}
    for _, div in ipairs(divs) do
        res = res .. lindex(char, math.floor(i/div) % 10)
    end
    return res
end

box.internal.sql_create_function("lindex", lindex)
box.internal.sql_create_function("int_to_char", int_to_char)

test:do_execsql_test(
        "skip-scan-1.1",
        [[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(a TEXT COLLATE "unicode_ci", b TEXT, c TEXT,
                d TEXT, e INT, f INT, PRIMARY KEY(c, b, a));
            WITH data(a, b, c, d, e, f) AS
            (SELECT int_to_char(0), 'xyz', 'zyx', '*', 0, 0 UNION ALL
            SELECT int_to_char(f+1), b, c, d, (e+1) % 2, f+1 FROM data WHERE f<1024)
            INSERT INTO t1 SELECT a, b, c, d, e, f FROM data;
            ANALYZE;
            SELECT COUNT(*) FROM t1 WHERE a < 'aaad';
            DROP TABLE t1;
        ]], {
            3
        })

test:do_execsql_test(
        "skip-scan-1.2",
        [[
            DROP TABLE IF EXISTS t2;
            CREATE TABLE t2(a TEXT COLLATE "unicode_ci", b TEXT, c TEXT,
                d TEXT, e INT, f INT, PRIMARY KEY(e, f));
            WITH data(a, b, c, d, e, f) AS
            (SELECT int_to_char(0), 'xyz', 'zyx', '*', 0, 0 UNION ALL
            SELECT int_to_char(f+1), b, c, d, (e+1) % 2, f+1 FROM data WHERE f<1024)
            INSERT INTO t2 SELECT a, b, c, d, e, f FROM data;
            ANALYZE;
            SELECT COUNT(*) FROM t2 WHERE f < 500;
            DROP TABLE t2;
        ]], {
            500
        }
)

test:do_execsql_test(
        "skip-scan-1.3",
        [[
            DROP TABLE IF EXISTS t3;
            CREATE TABLE t3(a TEXT COLLATE "unicode_ci", b TEXT, c TEXT,
                d TEXT, e INT, f INT, PRIMARY KEY(a));
            CREATE INDEX i31 ON t3(e, f);
            WITH data(a, b, c, d, e, f) AS
            (SELECT int_to_char(0), 'xyz', 'zyx', '*', 0, 0 UNION ALL
            SELECT int_to_char(f+1), b, c, d, (e+1) % 2, f+1 FROM data WHERE f<1024)
            INSERT INTO t3 SELECT a, b, c, d, e, f FROM data;
            ANALYZE;
            SELECT COUNT(*) FROM t3 WHERE f < 500;
            DROP INDEX i31 on t3;
            DROP TABLE t3;
        ]], {
            500
        }
)

test:do_execsql_test(
        "skip-scan-1.4",
        [[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(id INTEGER PRIMARY KEY, a TEXT, b INT, c INT, d INT);
            CREATE INDEX t1abc ON t1(a,b,c);
            DROP TABLE IF EXISTS t2;
            CREATE TABLE t2(id INTEGER PRIMARY KEY);
            INSERT INTO t2 VALUES(1);
            INSERT INTO t1 VALUES(1, 'abc',123,4,5);
            INSERT INTO t1 VALUES(2, 'abc',234,5,6);
            INSERT INTO t1 VALUES(3, 'abc',234,6,7);
            INSERT INTO t1 VALUES(4, 'abc',345,7,8);
            INSERT INTO t1 VALUES(5, 'def',567,8,9);
            INSERT INTO t1 VALUES(6, 'def',345,9,10);
            INSERT INTO t1 VALUES(7, 'bcd',100,6,11);
            ANALYZE;
            DELETE FROM "_sql_stat1";
            DELETE FROM "_sql_stat4";
            INSERT INTO "_sql_stat1" VALUES('t1','t1abc','10000 5000 2000 10');
            ANALYZE t2;
            SELECT a,b,c,d FROM t1 WHERE b=345;
        ]], {
            "abc", 345, 7, 8, "def", 345, 9, 10
        }
)


test:finish_test()
