#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(39)

local function do_xfer_test(test, test_func, test_name, func, exp, opts)
    local opts = opts or {}
    local exp_xfer_count = opts.exp_xfer_count
    local before = box.stat.sql().sql_xfer_count
    test_func(test, test_name, func, exp)
    local after = box.stat.sql().sql_xfer_count
    test:is(after - before, exp_xfer_count,
                   test_name .. '-xfer-count')
end

test.do_execsql_xfer_test = function(test, test_name, func, exp, opts)
    do_xfer_test(test, test.do_execsql_test, test_name, func, exp, opts)
end

test.do_catchsql_xfer_test = function(test, test_name, func, exp, opts)
    do_xfer_test(test, test.do_catchsql_test, test_name, func, exp, opts)
end

test:do_catchsql_xfer_test(
    "xfer-optimization-1.1",
    [[
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INTEGER UNIQUE);
        INSERT INTO t1 VALUES (1, 1), (2, 2), (3, 3);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INTEGER UNIQUE);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.1>
        0
        -- <xfer-optimization-1.1>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.2",
    [[
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.2>
        1, 1, 2, 2, 3, 3
        -- <xfer-optimization-1.2>
    })

test:do_catchsql_xfer_test(
    "xfer-optimization-1.3",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(id INTEGER PRIMARY KEY, b INTEGER);
        CREATE TABLE t2(id INTEGER PRIMARY KEY, b INTEGER);
        CREATE INDEX i1 ON t1(b);
        CREATE INDEX i2 ON t2(b);
        INSERT INTO t1 VALUES (1, 1), (2, 2), (3, 3);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.3>
        0
        -- <xfer-optimization-1.3>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.4",
    [[
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.5>
        1, 1, 2, 2, 3, 3
        -- <xfer-optimization-1.5>
    })

test:do_catchsql_xfer_test(
    "xfer-optimization-1.5",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INTEGER, c INTEGER);
        INSERT INTO t1 VALUES (1, 1, 2), (2, 2, 3), (3, 3, 4);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INTEGER);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.5>
        1, "table T2 has 2 columns but 3 values were supplied"
        -- <xfer-optimization-1.5>
    }, {
        exp_xfer_count = 0
    })

test:do_execsql_test(
    "xfer-optimization-1.6",
    [[
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.6>

        -- <xfer-optimization-1.6>
    })

test:do_catchsql_xfer_test(
    "xfer-optimization-1.7",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INTEGER);
        INSERT INTO t1 VALUES (1, 1), (2, 2), (3, 3);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INTEGER);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.7>
        0
        -- <xfer-optimization-1.7>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.8",
    [[
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.8>
        1, 1, 2, 2, 3, 3
        -- <xfer-optimization-1.8>
    })

test:do_catchsql_xfer_test(
    "xfer-optimization-1.9",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INTEGER);
        INSERT INTO t1 VALUES (1, 1), (2, 2), (3, 2);
        CREATE TABLE t2(b INTEGER, a INTEGER PRIMARY KEY);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.9>
        1, "Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"
        -- <xfer-optimization-1.9>
    }, {
        exp_xfer_count = 0
    })

test:do_execsql_test(
    "xfer-optimization-1.10",
    [[
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.10>

        -- <xfer-optimization-1.10>
    })

test:do_catchsql_xfer_test(
    "xfer-optimization-1.11",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INTEGER);
        INSERT INTO t1 VALUES (1, 1), (2, 2), (3, 2);
        CREATE TABLE t2(b INTEGER PRIMARY KEY, a INTEGER);
        INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.11>
        0
        -- <xfer-optimization-1.11>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.12",
    [[
            SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.12>
        1, 1, 2, 2, 3, 2
        -- <xfer-optimization-1.12>
    })

-- The following tests are supposed to test if xfer-optimization is actually
-- used in the given cases (if the conflict actually occurs):
-- 	1) insert w/o explicit confl. action & w/o index replace action
-- 	2) insert with abort
-- 	3.0) insert with rollback (into empty table)
-- 	3.1) insert with rollback (into non-empty table)
-- 	4) insert with replace
-- 	5) insert with fail
-- 	6) insert with ignore


-- 1) insert w/o explicit confl. action & w/o index replace action
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.13",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        INSERT INTO t2 VALUES (2, 2), (3, 4);
        START TRANSACTION;
            INSERT INTO t2 VALUES (4, 4);
            INSERT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.13>
        1, "Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"
        -- <xfer-optimization-1.13>
    }, {
        exp_xfer_count = 0
    })

test:do_execsql_test(
    "xfer-optimization-1.14",
    [[
            INSERT INTO t2 VALUES (10, 10);
        COMMIT;
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.14>
        2, 2, 3, 4, 4, 4, 10, 10
        -- <xfer-optimization-1.14>
    })

-- 2) insert with abort
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.20",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        INSERT INTO t2 VALUES (2, 2), (3, 4);
        START TRANSACTION;
            INSERT INTO t2 VALUES (4, 4);
            INSERT OR ABORT INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.20>
        1, "Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"
        -- <xfer-optimization-1.20>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.21",
    [[
            INSERT INTO t2 VALUES (10, 10);
        COMMIT;
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.21>
        2, 2, 3, 4, 4, 4, 10, 10
        -- <xfer-optimization-1.21>
    })

-- 3.0) insert with rollback (into empty table)
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.22",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        START TRANSACTION;
            INSERT OR ROLLBACK INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.22>
        0
        -- <xfer-optimization-1.22>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.23",
    [[
            INSERT INTO t2 VALUES (10, 10);
        COMMIT;
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.23>
        1, 1, 3, 3, 5, 5, 10, 10
        -- <xfer-optimization-1.23>
    })

-- 3.1) insert with rollback (into non-empty table)
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.24",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        INSERT INTO t2 VALUES (2, 2), (3, 4);
        START TRANSACTION;
            INSERT INTO t2 VALUES (4, 4);
            INSERT OR ROLLBACK INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.24>
        1, "Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"
        -- <xfer-optimization-1.24>
    }, {
        exp_xfer_count = 0
    })

test:do_execsql_test(
    "xfer-optimization-1.25",
    [[
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.25>
        2, 2, 3, 4
        -- <xfer-optimization-1.25>
    })

-- 4) insert with replace
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.26",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        INSERT INTO t2 VALUES (2, 2), (3, 4);
        START TRANSACTION;
            INSERT INTO t2 VALUES (4, 4);
            INSERT OR REPLACE INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.26>
        0
        -- <xfer-optimization-1.26>
    }, {
        exp_xfer_count = 0
    })

test:do_execsql_test(
    "xfer-optimization-1.27",
    [[
            INSERT INTO t2 VALUES (10, 10);
        COMMIT;
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.27>
        1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 10, 10
        -- <xfer-optimization-1.27>
    })

-- 5) insert with fail
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.28",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        INSERT INTO t2 VALUES (2, 2), (3, 4);
        START TRANSACTION;
            INSERT INTO t2 VALUES (4, 4);
            INSERT OR FAIL INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.28>
        1, "Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"
        -- <xfer-optimization-1.28>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.29",
    [[
            INSERT INTO t2 VALUES (10, 10);
        COMMIT;
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.29>
        1, 1, 2, 2, 3, 4, 4, 4, 10, 10
        -- <xfer-optimization-1.29>
    })

-- 6) insert with ignore
------------------------------------------------------------------------------

test:do_catchsql_xfer_test(
    "xfer-optimization-1.30",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT);
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES (1, 1), (3, 3), (5, 5);
        INSERT INTO t2 VALUES (2, 2), (3, 4);
        START TRANSACTION;
            INSERT INTO t2 VALUES (4, 4);
            INSERT OR IGNORE INTO t2 SELECT * FROM t1;
    ]], {
        -- <xfer-optimization-1.30>
        0
        -- <xfer-optimization-1.30>
    }, {
        exp_xfer_count = 1
    })

test:do_execsql_test(
    "xfer-optimization-1.31",
    [[
            INSERT INTO t2 VALUES (10, 10);
        COMMIT;
        SELECT * FROM t2;
    ]], {
        -- <xfer-optimization-1.31>
        1, 1, 2, 2, 3, 4, 4, 4, 5, 5, 10, 10
        -- <xfer-optimization-1.31>
    })

test:finish_test()
