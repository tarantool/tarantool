#!/usr/bin/env tarantool
local build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/sql-tap/?.so;'..build_path..'/test/sql-tap/?.dylib;'..package.cpath

local test = require("sqltester")
test:plan(3)

box.schema.func.create("gh-6024-funcs-return-bin.ret_bin", {
    language = "C",
    param_list = {},
    returns = "varbinary",
    exports = {"SQL"},
})

test:do_execsql_test(
    "gh-6024-1",
    [[
        SELECT typeof("gh-6024-funcs-return-bin.ret_bin"());
    ]], {
        "varbinary"
    })

box.schema.func.create("gh-6024-funcs-return-bin.ret_uuid", {
    language = "C",
    param_list = {},
    returns = "varbinary",
    exports = {"SQL"},
})

test:do_execsql_test(
    "gh-6024-2",
    [[
        SELECT typeof("gh-6024-funcs-return-bin.ret_uuid"());
    ]], {
        "varbinary"
    })

box.schema.func.create("gh-6024-funcs-return-bin.ret_decimal", {
    language = "C",
    param_list = {},
    returns = "varbinary",
    exports = {"SQL"},
})

test:do_execsql_test(
    "gh-6024-3",
    [[
        SELECT typeof("gh-6024-funcs-return-bin.ret_decimal"());
    ]], {
        "varbinary"
    })

box.schema.func.drop("gh-6024-funcs-return-bin.ret_bin")
box.schema.func.drop("gh-6024-funcs-return-bin.ret_uuid")
box.schema.func.drop("gh-6024-funcs-return-bin.ret_decimal")

test:finish_test()
