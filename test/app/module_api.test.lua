#!/usr/bin/env tarantool

box.cfg{logger = "tarantool.log"}

package.cpath = '../app/?.so;../app/?.dylib;'..package.cpath

local test = require('tap').test("module_api", function(test)
    test:plan(11)
    local status, module = pcall(require, 'module_api')
    test:ok(status, "module is loaded")
    if not status then
        return
    end

    for name, fun in pairs(module) do
        test:ok(fun, name .. " is ok")
    end
end)
os.exit(0)
