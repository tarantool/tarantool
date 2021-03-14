#!/usr/bin/env tarantool

local ffi = require('ffi')
local fio = require('fio')

box.cfg{log = "tarantool.log"}
-- Use BUILDDIR passed from test-run or cwd when run w/o
-- test-run to find test/app-tap/module_api.{so,dylib}.
local build_path = os.getenv("BUILDDIR") or '.'
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
        __tostring = function()
            return 'ok'
        end;
        __gc = function()
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
    obj = nil -- luacheck: no unused
    collectgarbage('collect')
    test:is(gc_counter, 1, 'pushcdata gc')
end

local function test_buffers(test, module)
    test:plan(8)
    local buffer = require('buffer')

    local bufalloc = ffi.new('char[?]', 128)
    local ibuf = buffer.ibuf()
    local pbuf = ibuf:alloc(128)
    local ibuf_ptr = ffi.cast('struct ibuf *', ibuf)

    test:ok(not module.toibuf(nil), 'toibuf of nil')
    test:ok(not module.toibuf({}), 'toibuf of {}')
    test:ok(not module.toibuf(1LL), 'toibuf of 1LL')
    test:ok(not module.toibuf(box.NULL), 'toibuf of box.NULL')
    test:ok(not module.toibuf(bufalloc), 'toibuf of allocated buffer')
    test:ok(module.toibuf(ibuf_ptr), "toibuf of ibuf*")
    test:ok(module.toibuf(ibuf), 'toibuf of ibuf')
    test:ok(not module.toibuf(pbuf), 'toibuf of pointer to ibuf data')
end

local function test_tuple_validate(test, module)
    test:plan(12)

    local nottuple1 = {}
    local nottuple2 = {true, 2}
    local nottuple3 = {false, nil, 2}
    local nottuple4 = {1, box.NULL, 2, 3}
    local tuple1 = box.tuple.new(nottuple1)
    local tuple2 = box.tuple.new(nottuple2)
    local tuple3 = box.tuple.new(nottuple3)
    local tuple4 = box.tuple.new(nottuple4)

    test:ok(not module.tuple_validate_def(nottuple1), "not tuple 1")
    test:ok(not module.tuple_validate_def(nottuple2), "not tuple 2")
    test:ok(not module.tuple_validate_def(nottuple3), "not tuple 3")
    test:ok(not module.tuple_validate_def(nottuple4), "not tuple 4")
    test:ok(module.tuple_validate_def(tuple1), "tuple 1")
    test:ok(module.tuple_validate_def(tuple2), "tuple 2")
    test:ok(module.tuple_validate_def(tuple3), "tuple 3")
    test:ok(module.tuple_validate_def(tuple4), "tuple 4")
    test:ok(not module.tuple_validate_fmt(tuple1), "tuple 1 (fmt)")
    test:ok(module.tuple_validate_fmt(tuple2), "tuple 2 (fmt)")
    test:ok(module.tuple_validate_fmt(tuple3), "tuple 3 (fmt)")
    test:ok(not module.tuple_validate_fmt(tuple4), "tuple 4 (fmt)")
end

local function test_iscallable(test, module)
    local ffi = require('ffi')

    ffi.cdef([[
        struct cdata_1 { int foo; };
        struct cdata_2 { int foo; };
    ]])

    local cdata_1 = ffi.new('struct cdata_1')
    local cdata_1_ref = ffi.new('struct cdata_1 &')
    local cdata_2 = ffi.new('struct cdata_2')
    local cdata_2_ref = ffi.new('struct cdata_2 &')

    local nop = function() end

    ffi.metatype('struct cdata_2', {
        __call = nop,
    })

    local cases = {
        {
            obj = nop,
            exp = true,
            description = 'function',
        },
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
            obj = {},
            exp = false,
            description = 'table without metatable',
        },
        {
            obj = setmetatable({}, {}),
            exp = false,
            description = 'table without __call metatable field',
        },
        {
            obj = setmetatable({}, {__call = nop}),
            exp = true,
            description = 'table with __call metatable field'
        },
        {
            obj = cdata_1,
            exp = false,
            description = 'cdata without __call metatable field',
        },
        {
            obj = cdata_1_ref,
            exp = false,
            description = 'cdata reference without __call metatable field',
        },
        {
            obj = cdata_2,
            exp = true,
            description = 'cdata with __call metatable field',
        },
        {
            obj = cdata_2_ref,
            exp = true,
            description = 'cdata reference with __call metatable field',
        },
    }

    test:plan(#cases)
    for _, case in ipairs(cases) do
        test:ok(module.iscallable(case.obj, case.exp), case.description)
    end
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

require('tap').test("module_api", function(test)
    test:plan(38)
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

    local _, msg = pcall(module.check_error)
    test:like(msg, 'luaT_error', 'luaT_error')

    test:test("pushcdata", test_pushcdata, module)
    test:test("iscallable", test_iscallable, module)
    test:test("iscdata", test_iscdata, module)
    test:test("buffers", test_buffers, module)
    test:test("tuple_validate", test_tuple_validate, module)

    space:drop()
end)

os.exit(0)
