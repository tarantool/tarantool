#!/usr/bin/env tarantool

--
-- gh-5632: Lua code should not use any global buffers or objects without
-- proper ownership protection. Otherwise these items might be suddenly reused
-- during Lua GC which happens almost at any moment. That might lead to data
-- corruption.
--

local tap = require('tap')
local ffi = require('ffi')
local uuid = require('uuid')

local function test_uuid(test)
    test:plan(1)

    local gc_count = 100
    local iter_count = 1000
    local is_success = true

    local function uuid_to_str()
        local uu = uuid.new()
        local str1 = uu:str()
        local str2 = uu:str()
        if str1 ~= str2 then
            is_success = false
            assert(false)
        end
    end

    local function create_gc()
        for _ = 1, gc_count do
            ffi.gc(ffi.new('char[1]'), function() uuid_to_str() end)
        end
    end

    for _ = 1, iter_count do
        create_gc()
        uuid_to_str()
    end

    test:ok(is_success, 'uuid in gc')
end

local test = tap.test('gh-5632-gc-buf-reuse')
test:plan(1)
test:test('uuid in __gc', test_uuid)

os.exit(test:check() and 0 or 1)
