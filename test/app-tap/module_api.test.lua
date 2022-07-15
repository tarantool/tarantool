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

-- Verify box_tuple_field_by_path() on several basic JSON path
-- selectors.
local function test_tuple_field_by_path(test, module)
    local msgpack = require('msgpack')
    local json = require('json')

    local space = box.schema.space.create('messages', {
        format = {
            {name = 'id', type = 'unsigned'},
            {name = 'from', type = 'map', is_nullable = true},
            {name = 'body', type = 'string', is_nullable = true},
            {name = 'tags', type = 'array', is_nullable = true},
        },
    })
    space:create_index('primary', {
        parts = {
            {'id', 'unsigned'},
        },
    })
    space:create_index('second_tag', {
        parts = {
            {'tags[2]', 'string'},
        },
    })

    local cases = {
        -- Malformed or invalid path, non existing field.
        {
            tuple = box.tuple.new({1, 2, 3}),
            path = '[2][2]',
            exp = nil,
            description = 'no such field',
        },
        {
            tuple = box.tuple.new({1, 2, 3}),
            path = '',
            exp = nil,
            description = 'empty JSON path',
        },
        {
            tuple = box.tuple.new({1, 2, 3}),
            path = '[',
            exp = nil,
            description = 'malformed JSON path',
        },
        {
            tuple = box.tuple.new({1, 2, 3}),
            path = '[*]',
            exp = nil,
            description = 'multikey JSON path (outmost field)',
        },
        {
            tuple = box.tuple.new({1, {2, 'x'}, 3}),
            path = '[2][*]',
            exp = nil,
            description = 'multikey JSON path (nested field)',
        },
        {
            tuple = box.tuple.new({1, 2, 3}),
            path = '.[2]',
            exp = nil,
            description = 'a period at beginning of a numeric path',
        },
        -- Named fields.
        {
            tuple = box.tuple.new({1, {foo = 'x'}, 3}),
            path = '[2].foo',
            exp = 'x',
            description = 'nested named field',
        },
        {
            tuple = space:replace({1}),
            path = 'id',
            exp = 1,
            description = 'outmost named field',
        },
        {
            tuple = space:replace({1, {id = 5, username = 'x'}}),
            path = 'from.username',
            exp = 'x',
            description = 'outmost and nested named fields',
        },
        {
            tuple = space:replace({1, {id = 5, username = 'x'}}),
            path = '.from.username',
            exp = 'x',
            description = 'a period at beginning',
        },
        -- Numeric selectors require extra attention in testing,
        -- because the index_base argument (0 as in C or 1 as in
        -- Lua) affects several code paths:
        --
        -- 1. How an outmost numeric field is interpreted.
        -- 2. How a nested numeric field is interpreted.
        -- 3. Usage of a precalculated field offset (for an
        --    indexed field).
        {
            tuple = box.tuple.new({1, 2, 3}),
            path = '[2]',
            exp = 2,
            description = 'outmost numeric field (no format)',
        },
        {
            tuple = box.tuple.new({1, {2, 'x'}, 3}),
            path = '[2][2]',
            exp = 'x',
            description = 'outmost and nested numeric fields (no format)',
        },
        {
            tuple = space:replace({1, nil, 'hi'}),
            path = '[3]',
            exp = 'hi',
            description = 'non-indexed outmost numeric field',
        },
        {
            -- The path points to a field right before an indexed
            -- one.
            tuple = space:replace({1, nil, nil, {'a', 'b', 'c'}}),
            path = '[4][1]',
            exp = 'a',
            description = 'non-indexed nested numeric field',
        },
        {
            -- The path points to a field right after an indexed
            -- one.
            tuple = space:replace({1, nil, nil, {'a', 'b', 'c'}}),
            path = '[4][3]',
            exp = 'c',
            description = 'non-indexed nested numeric field',
        },
        {
            tuple = space:replace({1}),
            path = '[1]',
            exp = 1,
            description = 'indexed outmost field',
        },
        {
            tuple = space:replace({1, nil, nil, {'a', 'b', 'c'}}),
            path = '[4][2]',
            exp = 'b',
            description = 'indexed nested field',
        },
    }

    local function check(test, case)
        local case_name = string.format(
            'tuple: %s; path: %s; index_base: %d; exp: %s',
            json.encode(case.tuple), case.path, case.index_base,
            json.encode(case.exp))
        local mp_field = module.tuple_field_by_path(case.tuple, case.path,
            case.index_base)

        local res
        if case.exp == nil then
            test:ok(mp_field == nil, case_name)
        else
            if mp_field == nil then
                res = 'NULL'
            else
                res = msgpack.decode(mp_field)
            end
            test:is_deeply(res, case.exp, case_name)
        end
        if case.index_base == 1 then
            test:is_deeply(res, case.tuple[case.path], 'consistency check')
        end
    end

    test:plan(#cases)
    for _, case in ipairs(cases) do
        test:test(case.description, function(test)
            test:plan(3)

            case.index_base = 1
            check(test, case)

            -- The same, but with zero based numeric selectors.
            case.index_base = 0
            case.path = case.path:gsub('%[%d+%]', function(selector)
                local num = tonumber(selector:sub(2, 2))
                return ('[%d]'):format(num - 1)
            end)
            check(test, case)
        end)
    end

    space:drop()
end

local function test_pushdecimal(test, module)
    local decimal = require('decimal')

    test:plan(4)

    local dec = module.decimal_mul('2.72', '2')
    test:ok(decimal.is_decimal(dec), 'decimal is pushed from C')
    test:is(tostring(dec), '5.44', 'got expected decimal value')

    local dec = module.decimal_div('5.44', '2')
    test:ok(decimal.is_decimal(dec), 'decimal is pushed from C')
    test:is(tostring(dec), '2.72', 'got expected decimal value')
end

local function test_isdecimal(test, module)
    local decimal = require('decimal')

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
            obj = 1LL,
            exp = false,
            description = 'cdata (number64)',
        },
        {
            obj = decimal.new('2.72'),
            exp = true,
            description = 'decimal',
        },
    }

    test:plan(#cases + 1)
    for _, case in ipairs(cases) do
        test:ok(module.isdecimal(case.obj, case.exp), case.description)
    end

    local ok = module.isdecimal_ptr(decimal.new('2.72'), '2.72')
    test:ok(ok, 'verify pointer from luaT_isdecimal()')
end

require('tap').test("module_api", function(test)
    test:plan(42)
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
    test:test("tuple_field_by_path", test_tuple_field_by_path, module)
    test:test("pushdecimal", test_pushdecimal, module)
    test:test("isdecimal", test_isdecimal, module)

    space:drop()
end)

os.exit(0)
