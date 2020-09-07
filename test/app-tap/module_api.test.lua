#!/usr/bin/env tarantool

local fio = require('fio')

box.cfg{log = "tarantool.log"}
build_path = os.getenv("BUILDDIR")
package.cpath = fio.pathjoin(build_path, 'test/app-tap/?.so'   ) .. ';' ..
                fio.pathjoin(build_path, 'test/app-tap/?.dylib') .. ';' ..
                package.cpath

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

local function test_iscdata(test, module)
    local ffi = require('ffi')
    ffi.cdef([[
        struct foo { int bar; };
    ]])

    local cases = {
        {
            obj = nil,
            exp = false,
            description = 'nil',
        },
        {
            obj = 1,
            exp = false,
            description = 'number',
        },
        {
            obj = 'hello',
            exp = false,
            description = 'string',
        },
        {
            obj = {},
            exp = false,
            description = 'table',
        },
        {
            obj = function() end,
            exp = false,
            description = 'function',
        },
        {
            obj = ffi.new('struct foo'),
            exp = true,
            description = 'cdata',
        },
        {
            obj = ffi.new('struct foo *'),
            exp = true,
            description = 'cdata pointer',
        },
        {
            obj = ffi.new('struct foo &'),
            exp = true,
            description = 'cdata reference',
        },
        {
            obj = 1LL,
            exp = true,
            description = 'cdata number',
        },
    }

    test:plan(#cases)
    for _, case in ipairs(cases) do
        test:ok(module.iscdata(case.obj, case.exp), case.description)
    end
end

local test = require('tap').test("module_api", function(test)
    test:plan(27)
    local status, module = pcall(require, 'module_api')
    test:is(status, true, "module")
    test:ok(status, "module is loaded")
    if not status then
        test:diag("Failed to load library:")
        for _, line in ipairs(module:split("\n")) do
            test:diag("%s", line)
        end
        return
    end

    local space  = box.schema.space.create("test")
    space:create_index('primary')

    for name, fun in pairs(module) do
        if string.sub(name,1, 5) == 'test_' then
            test:ok(fun(), name .. " is ok")
        end
    end

    local status, msg = pcall(module.check_error)
    test:like(msg, 'luaT_error', 'luaT_error')

    test:test("pushcdata", test_pushcdata, module)
    test:test("iscdata", test_iscdata, module)

    space:drop()
end)

os.exit(0)
