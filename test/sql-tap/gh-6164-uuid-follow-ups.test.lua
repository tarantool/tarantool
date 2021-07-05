#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

-- Make sure that function quote() can work with uuid.
test:do_execsql_test(
    "gh-6164-1",
    [[
        SELECT quote(cast('11111111-1111-1111-1111-111111111111' as uuid));
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

test:finish_test()
