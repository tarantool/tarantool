#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(3)

-- gh-3231: make sure that there is no redundant OP_Goto at the
-- start of VDBE program. In other words OP_Init jumps exactly to
-- the next opcode (i.e. opcode with address 1).
--
test:do_execsql_test(
    "explain-1.0",
    [[
        CREATE TABLE t1(id INTEGER PRIMARY KEY, a INT);
        INSERT INTO t1 VALUES(1, 2), (3, 4), (5, 6);
        SELECT * FROM t1;
    ]], {
        -- <explain-1.0>
        1, 2, 3, 4, 5, 6
        -- </explain-1.0>
    })

test:do_test(
    "explain-1.1",
    function()
        local opcodes = test:execsql("EXPLAIN SELECT * FROM t1;")
        return opcodes[1]
    end,
        -- <explain-1.1>
        0, 'Init', 0, 1, 0, '', '00', 'Start at 1'
        -- </explain-1.1>
    )

test:do_test(
    "explain-1.2",
    function()
        local opcodes = test:execsql("EXPLAIN SELECT a + 1 FROM t1 WHERE id = 4 OR id = 5;")
        return opcodes[1]
    end,
        -- <explain-1.2>
        0, 'Init', 0, 1, 0, '', '00', 'Start at 1'
        -- </explain-1.2>
    )

test:finish_test()
