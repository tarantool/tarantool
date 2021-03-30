#!/usr/bin/env tarantool
local build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/sql-tap/?.so;'..build_path..'/test/sql-tap/?.dylib;'..package.cpath

local test = require("sqltester")
test:plan(2)

box.schema.func.create("gh-5938-wrong-string-length.ret_str", {
    language = "C",
    param_list = { "string" },
    returns = "string",
    exports = { "LUA", "SQL" },
    is_deterministic = true
})

test:execsql([[CREATE TABLE t (i INT PRIMARY KEY, s STRING);]])
box.space.T:insert({1, 'This is a complete string'})
box.space.T:insert({2, 'This is a cropped\0 string'})

test:do_execsql_test(
    "gh-5938-1",
    [[
        SELECT "gh-5938-wrong-string-length.ret_str"(s) from t;
    ]], {
        "This is a complete string","This is a cropped\0 string"
    })

box.schema.func.create("ret_str", {
    language = "Lua",
    body = [[function(str) return str end]],
    param_list = { "string" },
    returns = "string",
    exports = { "LUA", "SQL" },
    is_deterministic = true
})

test:do_execsql_test(
    "gh-5938-2",
    [[
        SELECT "ret_str"(s) from t;
    ]], {
        "This is a complete string","This is a cropped\0 string"
    })

test:finish_test()
