#!/usr/bin/env tarantool

box.cfg{logger = "tarantool.log"}

package.cpath = '../app-tap/?.so;../app-tap/?.dylib;'

local function test_pushcdata(test, module)
    test:plan(6)
    local ffi = require('ffi')
    ffi.cdef('struct module_api_test { int a; };')
    local gc_counter = 0;
    local ct = ffi.typeof('struct module_api_test')
    ffi.metatype(ct, {
        __tostring = function(obj)
            return 'ok'
        end;
        __gc = function(obj)
            gc_counter = gc_counter + 1;
        end
    })

    local ctid = tonumber(ct)
    local obj, ptr = module.pushcdata(ctid)
    test:is(ffi.typeof(obj), ct, 'pushcdata typeof')
    test:is(tostring(obj), 'ok', 'pushcdata metatable')
    local ctid2, ptr2 = module.checkcdata(obj)
    test:is(ctid, ctid2, 'checkcdata type')
    test:is(ptr, ptr2, 'checkcdata value')
    test:is(gc_counter, 0, 'pushcdata gc')
    obj = nil
    collectgarbage('collect')
    test:is(gc_counter, 1, 'pushcdata gc')
end

local test = require('tap').test("module_api", function(test)
    test:plan(15)
    local status, module = pcall(require, 'module_api')
    test:ok(status, "module is loaded")
    if not status then
        return
    end

    for name, fun in pairs(module) do
        if string.sub(name,1, 5) == 'test_' then
            test:ok(fun, name .. " is ok")
        end
    end

    test:test("pushcdata", test_pushcdata, module)
end)
os.exit(0)
