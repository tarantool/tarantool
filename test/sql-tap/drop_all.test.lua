#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(3)
local prefix = "drop_all-"


local N = 10
test:do_test(
    prefix.."1.0",
    function()
        for i = 1, N do
            test:execsql(string.format("create table table%s(a INT primary key)", i))
        end

        for i = 1, N do
            test:execsql(string.format("create view view%s as select * from table%s", i, i))
        end
    end,
    nil)


test:do_test(
    prefix.."-1.1",
    function()
        return test:drop_all_views()
    end,
    N
)

test:do_test(
    prefix.."-1.1",
    function()
        return test:drop_all_tables()
    end,
    N
)

test:finish_test()

