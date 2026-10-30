#!/usr/bin/env tarantool

--
-- gh-6025: box.on_commit() and box.on_rollback() triggers always leaked.
--

local tap = require('tap')

--
-- Create a functional reference at the passed value. Not at the variable
-- keeping it in the caller, but at the value itself.
--
local function wrap_value(value)
    return function()
        assert(value == nil or value ~= nil)
    end
end

local function test_on_commit_rollback(test)
    test:plan(2)

    local s = box.schema.create_space('test')
    s:create_index('pk')
    local weak_ref = setmetatable({}, {__mode = 'v'})

    -- If the triggers are deleted, the wrapped value reference must disappear
    -- and nothing should keep the value from collection from the weak table.
    local value = {}
    weak_ref[1] = value
    box.begin()
    s:replace{1}
    box.on_commit(wrap_value(value))
    box.on_rollback(wrap_value(value))
    box.commit()
    value = nil -- luacheck: ignore
    collectgarbage()
    test:isnil(weak_ref[1], 'on commit both triggers are deleted')

    value = {}
    weak_ref[1] = value
    box.begin()
    s:replace{2}
    box.on_commit(wrap_value(value))
    box.on_rollback(wrap_value(value))
    box.rollback()
    value = nil -- luacheck: ignore
    collectgarbage()
    test:isnil(weak_ref[1], 'on rollback both triggers are deleted')
end

box.cfg{}

local test = tap.test('gh-6025-box.on_commit-leak')
test:plan(1)
test:test('delete triggers on commit and rollback', test_on_commit_rollback)

os.exit(test:check() and 0 or 1)
