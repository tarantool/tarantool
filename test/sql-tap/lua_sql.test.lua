#!/usr/bin/env tarantool
local test = require("sqltester")
local NULL = require('msgpack').NULL
test:plan(24)

test:do_test(
    "lua_sql-1.0",
    function ()
        box.schema.func.create('FUNC1', {language = 'Lua',
                         is_deterministic = true,
                         body = 'function(a) return 2 end',
                         param_list = {'scalar'}, returns = 'integer',
                         exports = {'LUA', 'SQL'}})
        return test:execsql("select func1(1)")
    end,
    {2})

-- new function should replace prewious one
test:do_test(
    "lua_sql-1.1",
    function ()
        box.func.FUNC1:drop()
        box.schema.func.create('FUNC1', {language = 'Lua',
                        is_deterministic = true,
                        body = 'function(a) return a end',
                        param_list = {'scalar'}, returns = 'scalar',
                        exports = {'LUA', 'SQL'}})
        return test:execsql("select func1(1)")
    end,
    {1})

test:do_catchsql_test(
    "lua_sql-1.0",
    "select func3(1)",
    {1, "Function 'FUNC3' does not exist"})


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

_G.from_sql_to_lua = from_sql_to_lua

box.schema.func.create('CHECK_FROM_SQL_TO_LUA', {language = 'Lua',
                       is_deterministic = true,
                       body = [[
                           function(i, arg)
                               if from_sql_to_lua[i][2] == arg then
                                   return 1
                               end
                               return 0
                           end
                       ]],
                       param_list = {'integer', 'scalar'}, returns = 'integer',
                       exports = {'LUA', 'SQL'}})

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
    [4] = {"true", true},
    [5] = {"false", false},
    [6] = {12, 12LL},
}

_G.from_lua_to_sql = from_lua_to_sql

box.schema.func.create('CHECK_FROM_LUA_TO_SQL', {language = 'Lua',
                       is_deterministic = true,
                       body = [[
                           function(i)
                               return from_lua_to_sql[i][2]
                           end
                       ]],
                       param_list = {'integer'}, returns = 'any',
                       exports = {'LUA', 'SQL'}})

-- check for different types
for i = 1, #from_lua_to_sql, 1 do
    test:do_execsql_test(
        "lua_sql-2.3."..i,
        "select "..tostring(from_lua_to_sql[i][1]).." = check_from_lua_to_sql("..i..")",
        {true})
end

test:do_execsql_test("lua_sql-2.4.1", "SELECT LUA('return box.NULL') is NULL", {true})
test:do_execsql_test("lua_sql-2.4.2", "SELECT LUA('return nil') is NULL", {true})
test:do_execsql_test("lua_sql-2.4.3", "SELECT LUA('ffi = require(\"ffi\") return ffi.new(\"uint64_t\", (2LLU^64)-1)') = 18446744073709551615", {true})

box.schema.func.create('ALLWAYS_ERROR', {language = 'Lua',
                       is_deterministic = true,
                       body = [[
                           function()
                               error("my_error123")
                               return 1
                           end
                       ]],
                       param_list = {}, returns = 'integer',
                       exports = {'LUA', 'SQL'}})


test:do_catchsql_test(
    "lua_sql-2.6",
    "select allways_error()",
    {1, "/my_error123/"})

box.func.FUNC1:drop()
box.func.CHECK_FROM_SQL_TO_LUA:drop()
box.func.CHECK_FROM_LUA_TO_SQL:drop()
box.func.ALLWAYS_ERROR:drop()

test:finish_test()
