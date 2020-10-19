#!/usr/bin/env tarantool

local test = require("sqltester")
test:plan(9)

local expected_err = "Can't modify data because this instance is in read-only mode."

test:execsql([[
    CREATE TABLE TEST (A INT, B INT, PRIMARY KEY (A));
    INSERT INTO TEST (A, B) VALUES (3, 3);
]])

box.cfg{read_only = true}

test:do_catchsql_test(
    "gh-5231-1",
    [[
        INSERT INTO TEST (A, B) VALUES (1, 1);
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-2",
    [[
        DELETE FROM TEST;
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-3",
    [[
        REPLACE INTO TEST VALUES (1, 2);
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-4",
    [[
        UPDATE TEST SET B=4 WHERE A=3;
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-5",
    [[
        TRUNCATE TABLE TEST;
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-6",
    [[
        CREATE TABLE TEST2 (A INT, PRIMARY KEY (A));
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-7",
    [[
        ALTER TABLE TEST ADD CONSTRAINT UK_CON UNIQUE (B);
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-8",
    [[
        ALTER TABLE TEST RENAME TO TEST2;
    ]], {
        1, expected_err
    })

test:do_catchsql_test(
    "gh-5231-9",
    [[
        DROP TABLE TEST;
    ]], {
        1, expected_err
    })

test:finish_test()
