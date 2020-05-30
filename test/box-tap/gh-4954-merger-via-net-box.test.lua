#!/usr/bin/env tarantool

local merger_lib = require('merger')
local buffer = require('buffer')
local msgpack = require('msgpack')
local net_box = require('net.box')
local fiber = require('fiber')
local tap = require('tap')


-- {{{ Helpers

-- Lua iterator generator function to iterate over an array.
local function array_next(arr, idx)
    idx = idx or 1
    local item = arr[idx]
    if item == nil then
        return
    end
    return idx + 1, item
end

-- Lua iterator generator to iterate over an array with yields.
local function array_yield_next(arr, idx)
    fiber.sleep(0)
    return array_next(arr, idx)
end

-- }}}

-- {{{ Code that is run in a background fiber (via net.box)

local function use_table_source(tuples)
    local source = merger_lib.new_source_fromtable(tuples)
    return source:select()
end
_G.use_table_source = use_table_source

local function use_buffer_source(tuples)
    local buf = buffer.ibuf()
    msgpack.encode(tuples, buf)
    local source = merger_lib.new_source_frombuffer(buf)
    return source:select()
end
_G.use_buffer_source = use_buffer_source

local function use_tuple_source(tuples)
    local source = merger_lib.new_tuple_source(array_next, tuples)
    return source:select()
end
_G.use_tuple_source = use_tuple_source

local function use_table_source_yield(tuples)
    local chunks = {}
    for i, t in ipairs(tuples) do
        chunks[i] = {t}
    end
    local source = merger_lib.new_table_source(array_yield_next, chunks)
    return source:select()
end
_G.use_table_source_yield = use_table_source_yield

local function use_buffer_source_yield(tuples)
    local buffers = {}
    for i, t in ipairs(tuples) do
        buffers[i] = buffer.ibuf()
        msgpack.encode({t}, buffers[i])
    end
    local source = merger_lib.new_buffer_source(array_yield_next, buffers)
    return source:select()
end
_G.use_buffer_source_yield = use_buffer_source_yield

local function use_tuple_source_yield(tuples)
    local source = merger_lib.new_tuple_source(array_yield_next, tuples)
    return source:select()
end
_G.use_tuple_source_yield = use_tuple_source_yield

-- }}}

box.cfg({
    listen = os.getenv('LISTEN') or 'localhost:3301'
})
box.schema.user.grant('guest', 'execute', 'universe', nil,
                      {if_not_exists = true})

local test = tap.test('gh-4954-merger-via-net-box.test.lua')
test:plan(6)

local tuples = {
    {1},
    {2},
    {3},
}

local connection = net_box.connect(box.cfg.listen)

local res = connection:call('use_table_source', {tuples})
test:is_deeply(res, tuples, 'verify table source')
local res = connection:call('use_buffer_source', {tuples})
test:is_deeply(res, tuples, 'verify buffer source')
local res = connection:call('use_tuple_source', {tuples})
test:is_deeply(res, tuples, 'verify tuple source')

local function test_verify_source_async(test, func_name, request_count)
    test:plan(request_count)

    local futures = {}
    for _ = 1, request_count do
        local future = connection:call(func_name, {tuples}, {is_async = true})
        table.insert(futures, future)
    end
    for i = 1, request_count do
        local res = unpack(futures[i]:wait_result())
        test:is_deeply(res, tuples, ('verify request %d'):format(i))
    end
end

test:test('verify table source, which yields', test_verify_source_async,
          'use_table_source_yield', 100)
test:test('verify buffer source, which yields', test_verify_source_async,
          'use_buffer_source_yield', 100)
test:test('verify tuple source, which yields', test_verify_source_async,
          'use_tuple_source_yield', 100)

box.schema.user.revoke('guest', 'execute', 'universe')

os.exit(test:check() and 0 or 1)
