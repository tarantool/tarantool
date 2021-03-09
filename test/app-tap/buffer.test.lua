#!/usr/bin/env tarantool

local tap = require('tap')
local fiber = require('fiber')
local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put
local cord_ibuf_drop = buffer.internal.cord_ibuf_drop

local function test_cord_ibuf(test)
    test:plan(10)

    local ibuf1 = cord_ibuf_take()
    test:is(ibuf1:size(), 0, 'is empty')
    ibuf1:alloc(1)
    test:is(ibuf1:size(), 1, 'alloc 1')
    cord_ibuf_put(ibuf1)

    ibuf1 = cord_ibuf_take()
    test:is(ibuf1:size(), 0, 'is empty again')
    ibuf1:alloc(1)
    cord_ibuf_drop(ibuf1)

    ibuf1 = cord_ibuf_take()
    test:is(ibuf1:capacity(), 0, 'has no capacity')
    local pos1 = ibuf1:alloc(1)
    pos1[0] = 1

    local ibuf2 = cord_ibuf_take()
    test:isnt(ibuf1, ibuf2, 'can have 2 cord buffers')
    test:is(ibuf2:size(), 0, 'second is empty')
    local pos2 = ibuf2:alloc(1)
    pos2[0] = 2
    test:is(pos1[0], 1, 'change does not affect the first buffer')
    cord_ibuf_put(ibuf2)
    ibuf1 = ibuf2

    fiber.yield()
    ibuf2 = cord_ibuf_take()
    test:is(ibuf1, ibuf2, 'yield drops the ownership')
    cord_ibuf_put(ibuf2)

    ibuf1 = nil
    local f = fiber.new(function()
        ibuf1 = cord_ibuf_take()
    end)
    f:set_joinable(true)
    f:join()
    test:isnt(ibuf1, nil, 'took a cord buf in a new fiber')
    ibuf2 = cord_ibuf_take()
    test:is(ibuf1, ibuf2, 'was freed on fiber stop and reused')
    cord_ibuf_put(ibuf2)
end

local test = tap.test('buffer')
test:plan(1)
test:test("cord buffer", test_cord_ibuf)

os.exit(test:check() and 0 or 1)
