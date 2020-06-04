#!/usr/bin/env tarantool

--
-- gh-4954: The fiber-local Lua stack should be even after
-- merge_source_next() call from C code.
--
-- See test/box-tap/check_merge_source.c for more information.
--

local fio = require('fio')

-- Use BUILDDIR passed from test-run or cwd when run w/o
-- test-run to find test/box-tap/check_merge_source.{so,dylib}.
local build_path = os.getenv('BUILDDIR') or '.'
package.cpath = fio.pathjoin(build_path, 'test/box-tap/?.so'   ) .. ';' ..
                fio.pathjoin(build_path, 'test/box-tap/?.dylib') .. ';' ..
                package.cpath

local buffer = require('buffer')
local msgpack = require('msgpack')
local merger = require('merger')
local tap = require('tap')
local check_merge_source = require('check_merge_source')

-- {{{ Lua iterator generator functions

local function triplet()
    return 1, 2, 3
end

local function wrong_type()
    return 1, 2
end

local function no_chunks()
    return nil
end

local function bad_buffer()
    local buf = buffer.ibuf()
    msgpack.encode({foo = 'bar'}, buf)
    return 1, buf
end

-- FIXME: Enable after gh-5048: ('non-array tuple in a buffer
-- leads to assertion fail').
--[[
local function bad_tuple_in_buffer()
    local tuple = 1
    local buf = buffer.ibuf()
    msgpack.encode({tuple}, buf)
    return 1, buf
end
]]--

local function empty_buffer(_, state)
    if state ~= nil then
        return nil
    end
    local buf = buffer.ibuf()
    return 1, buf
end

local function no_tuples_buffer(_, state)
    if state ~= nil then
        return nil
    end
    local buf = buffer.ibuf()
    msgpack.encode({}, buf)
    return 1, buf
end

local function good_buffer(_, state)
    if state ~= nil then
        return nil
    end
    local buf = buffer.ibuf()
    local tuple = {1, 2, 3}
    msgpack.encode({tuple}, buf)
    return 1, buf
end

local function bad_tuple_in_table()
    local tuple = 1
    return 1, {tuple}
end

local function empty_table(_, state)
    if state ~= nil then
        return nil
    end
    return 1, {}
end

local function good_table(_, state)
    if state ~= nil then
        return nil
    end
    local tuple = {1, 2, 3}
    return 1, {tuple}
end

local function bad_tuple()
    local tuple = 1
    return 1, tuple
end

local function good_tuple(_, state)
    if state ~= nil then
        return nil
    end
    local tuple = {1, 2, 3}
    return 1, tuple
end

-- }}}

local cases = {
    {
        'buffer source, bad gen function',
        source_new = merger.new_buffer_source,
        source_gen = triplet,
        exp_err = '^Expected <state>, <buffer>, got 3 return values$',
    },
    {
        'buffer source, bad gen result',
        source_new = merger.new_buffer_source,
        source_gen = wrong_type,
        exp_err = '^Expected <state>, <buffer>$',
    },
    {
        'buffer source, bad buffer',
        source_new = merger.new_buffer_source,
        source_gen = bad_buffer,
        exp_err = '^Invalid merge source 0x[0-9a-f]+$',
    },
    -- FIXME: Enable after gh-5048: ('non-array tuple in a buffer
    -- leads to assertion fail').
    --[[
    {
        'buffer source, bad tuple in buffer',
        source_new = merger.new_buffer_source,
        source_gen = bad_tuple_in_buffer,
        exp_err = '^A tuple must be an array$',
    },
    ]]--
    {
        'buffer source, no buffers',
        source_new = merger.new_buffer_source,
        source_gen = no_chunks,
    },
    {
        'buffer source, empty buffer',
        source_new = merger.new_buffer_source,
        source_gen = empty_buffer,
    },
    {
        'buffer source, no tuples buffer',
        source_new = merger.new_buffer_source,
        source_gen = no_tuples_buffer,
    },
    {
        'buffer source, good buffer',
        source_new = merger.new_buffer_source,
        source_gen = good_buffer,
    },
    {
        'table source, bad gen function',
        source_new = merger.new_table_source,
        source_gen = triplet,
        exp_err = '^Expected <state>, <table>, got 3 return values$',
    },
    {
        'table source, bad gen result',
        source_new = merger.new_table_source,
        source_gen = wrong_type,
        exp_err = '^Expected <state>, <table>$',
    },
    {
        'table source, bad tuple in table',
        source_new = merger.new_table_source,
        source_gen = bad_tuple_in_table,
        exp_err = '^A tuple or a table expected, got number$',
    },
    {
        'buffer source, no tables',
        source_new = merger.new_table_source,
        source_gen = no_chunks,
    },
    {
        'table source, empty table',
        source_new = merger.new_table_source,
        source_gen = empty_table,
    },
    {
        'table source, good table',
        source_new = merger.new_table_source,
        source_gen = good_table,
    },
    {
        'tuple source, bad gen function',
        source_new = merger.new_tuple_source,
        source_gen = triplet,
        exp_err = '^Expected <state>, <tuple>, got 3 return values$',
    },
    {
        'tuple source, bad gen result',
        source_new = merger.new_tuple_source,
        source_gen = wrong_type,
        exp_err = '^A tuple or a table expected, got number$',
    },
    {
        'tuple source, bad tuple',
        source_new = merger.new_tuple_source,
        source_gen = bad_tuple,
        exp_err = '^A tuple or a table expected, got number$',
    },
    {
        'tuple source, no tuples',
        source_new = merger.new_tuple_source,
        source_gen = no_chunks,
    },
    {
        'tuple source, good tuple',
        source_new = merger.new_tuple_source,
        source_gen = good_tuple,
    },
}

local test = tap.test('gh-4954-merger-via-c')
test:plan(#cases)

for _, case in ipairs(cases) do
    test:test(case[1], function(test)
        test:plan(3)
        local source = case.source_new(case.source_gen)
        local is_next_ok, err_msg, is_stack_even =
            check_merge_source.call_next(source)
        if case.exp_err == nil then
            test:ok(is_next_ok, 'merge_source_next() should succeed')
            test:ok(err_msg == nil, 'no error message')
        else
            test:ok(not is_next_ok, 'merge_source_next() should fail')
            test:ok(string.match(err_msg, case.exp_err), 'verify error message',
                                 {err_msg = err_msg, exp_err = case.exp_err})
        end
        test:ok(is_stack_even, 'fiber-local Lua stack should be even')
    end)
end

os.exit(test:check() and 0 or 1)
