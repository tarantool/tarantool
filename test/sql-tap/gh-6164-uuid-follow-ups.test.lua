#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(9)

-- Make sure that function quote() can work with uuid.
test:do_execsql_test(
    "gh-6164-1",
    [[
        SELECT QUOTE(cast('11111111-1111-1111-1111-111111111111' as uuid));
    ]], {
        '11111111-1111-1111-1111-111111111111'
    })

-- Make sure that uuid value can be binded.
local uuid1 = require('uuid').fromstr('11111111-1111-1111-1111-111111111111')
local uuid2 = require('uuid').fromstr('11111111-2222-1111-1111-111111111111')
local uuid3 = require('uuid').fromstr('11111111-1111-3333-1111-111111111111')
test:do_test(
    "gh-6164-2",
    function()
        return box.execute([[SELECT ?;]], {uuid1}).rows[1][1]
    end,
    uuid1)
test:do_test(
    "gh-6164-3",
    function()
        return box.execute([[SELECT $2;]], {123, uuid2}).rows[1][1]
    end,
    uuid2)

test:do_test(
    "gh-6164-4",
    function()
        return box.execute([[SELECT :two;]], {{[":two"] = uuid3}}).rows[1][1]
    end,
    uuid3)

--
-- Make sure a comparison that includes a UUID and follows the SCALAR rules is
-- working correctly.
--
box.execute([[CREATE TABLE t (i INTEGER PRIMARY KEY, s SCALAR);]])
box.execute([[INSERT INTO t VALUES (1, ?)]], {uuid1})

test:do_execsql_test(
    "gh-6164-5",
    [[
        SELECT GREATEST(i, s, x'33', 'something') FROM t;
    ]], {
        uuid1
    })

test:do_execsql_test(
    "gh-6164-6",
    [[
        SELECT LEAST(i, s, x'33', 'something') FROM t;
    ]], {
        1
    })

box.execute([[INSERT INTO t VALUES (2, 2);]])

test:do_execsql_test(
    "gh-6164-7",
    [[
        SELECT MAX(s) FROM t;
    ]], {
        uuid1
    })

test:do_execsql_test(
    "gh-6164-8",
    [[
        SELECT MIN(s) FROM t;
    ]], {
        2
    })

box.execute([[DELETE FROM t;]])

box.execute([[INSERT INTO t VALUES (1, X'00'), (2, ?);]], {uuid1})

test:do_execsql_test(
    "gh-6164-9",
    [[
        SELECT i FROM t ORDER BY s;
    ]], {
        1, 2
    })

box.execute([[DROP TABLE t;]])

test:finish_test()
