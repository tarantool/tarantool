#!/usr/bin/env tarantool
test = require("sqltester")
NULL = require('msgpack').NULL
test:plan(24)

local function func1(a)
    return a
end
local function allways_2(a)
    return 2
end

test:do_test(
    "lua_sql-1.0",
    function ()
        box.internal.sql_create_function("func1", allways_2)
        return test:execsql("select func1(1)")
    end,
    {2})

-- new function should replace prewious one
test:do_test(
    "lua_sql-1.1",
    function ()
        box.internal.sql_create_function("func1", func1)
        return test:execsql("select func1(1)")
    end,
    {1})

-- try to loose memory
test:do_test(
    "lua_sql-1.2",
    function ()
        for i = 1, 1000000, 1 do
            box.internal.sql_create_function("func1", func1)
        end
        return test:execsql("select func1(1)")
    end,
    {1})

-- check sqlite polymorphism
test:do_test(
    "lua_sql-1.3",
    function ()
        box.internal.sql_create_function("allways_2", allways_2, 1) -- specify 1 arg
        box.internal.sql_create_function("allways_2", func1)
        box.internal.sql_create_function("allways_2", func1, 2)
        box.internal.sql_create_function("allways_2", func1, 3)
        return test:execsql("select allways_2(1)")
    end,
    {2})

test:do_catchsql_test(
    "lua_sql-1.0",
    "select func3(1)",
    {1, "no such function: FUNC3"})


-- check for different types
for _, val in ipairs({
        {1, "'val'", "val"},
        {2, 1, 1},
        {3, "1.0", 1},
        {4, 1.5, 1.5},
    }) do
    local tn = val[1]
    local param = val[2]
    local result = val[3]
    test:do_execsql_test(
        "lua_sql-2.1."..tn,
        "select func1("..param..")",
        {result})
end

local from_sql_to_lua = {
    [1] = {1, 1},
    [2] = {"1", 1},
    [3] = {"1.5", 1.5},
    [4] = {"'1'", "1"},
    [5] = {"NULL", NULL},
    [6] = {"x'0500'", "\u{0005}\u{0000}"},
    [7] = {"123123123123123", 123123123123123LL},
}
local json = require("json")
local function check_from_sql_to_lua(i, arg)
    if from_sql_to_lua[i][2] == arg then
        return 1
    end
    return 0
end
box.internal.sql_create_function("check_from_sql_to_lua", check_from_sql_to_lua)

-- check for different types
for i = 1, #from_sql_to_lua, 1 do
    test:do_execsql_test(
        "lua_sql-2.2."..i,
        "select check_from_sql_to_lua("..i..","..from_sql_to_lua[i][1]..")",
        {1})
end

local from_lua_to_sql = {
    [1] = {1, 1},
    [2] = {"1.5", 1.5},
    [3] = {"'1'", "1"},
    [4] = {1, true},
    [5] = {0, false},
}
local function check_from_lua_to_sql(i)
    return from_lua_to_sql[i][2]
end
box.internal.sql_create_function("check_from_lua_to_sql", check_from_lua_to_sql)

-- check for different types
for i = 1, #from_lua_to_sql, 1 do
    test:do_execsql_test(
        "lua_sql-2.3."..i,
        "select "..from_lua_to_sql[i][1].." = check_from_lua_to_sql("..i..")",
        {1})
end

local from_lua_to_sql_bad = {
    [1] = NULL,
    [2] = 12LL, -- it is possible to support this type
}
local function check_from_lua_to_sql_bad(i)
    return from_lua_to_sql_bad[i]
end
box.internal.sql_create_function("check_from_lua_to_sql_bad", check_from_lua_to_sql_bad)

for i = 1, #from_lua_to_sql_bad, 1 do
    test:do_catchsql_test(
        "lua_sql-2.5."..i,
        "select check_from_lua_to_sql_bad("..i..")",
        {1, "/Unsupported/"})
end

local function allways_error()
    error("my_error123")
    return 1
end
box.internal.sql_create_function("allways_error", allways_error)

test:do_catchsql_test(
    "lua_sql-2.6",
    "select allways_error()",
    {1, "/my_error123/"})


test:finish_test()
