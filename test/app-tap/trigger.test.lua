#!/usr/bin/env tarantool

local table_clear = require('table.clear')

box.cfg{
    log = "tarantool.log"
}

local trigger = require('internal.trigger')
local test = require('tap').test('trigger')

test:plan(3)

local trigger_list = trigger.new("sweet trigger")
test:ok(trigger_list ~= nil, "test that trigger list is created")

test:test("simple trigger test", function(test)
    test:plan(10)

    local cnt = 0
    local function trigger_cnt() cnt = cnt + 1 end

    -- Append first trigger
    trigger_list(trigger_cnt)
    trigger_list:run()
    test:is(cnt, 1, "check first run")
    -- Append second trigger
    trigger_list(trigger_cnt)
    trigger_list:run()
    test:is(cnt, 3, "check first run")
    -- Check listing
    local list_copy = trigger_list()
    test:is(#list_copy, 2, "trigger() count")
    table.remove(list_copy)
    test:is(#trigger_list(), 2, "check that we've returned copy")

    -- Delete both triggers
    test:is(trigger_list(nil, trigger_cnt), trigger_cnt, "pop trigger")
    trigger_list:run()
    test:is(#trigger_list(), 1, "check trigger count after delete")
    test:is(cnt, 4, "check third run")
    test:is(trigger_list(nil, trigger_cnt), trigger_cnt, "pop trigger")
    trigger_list:run()
    test:is(#trigger_list(), 0, "check trigger count after delete")


    -- Check that we've failed to delete trigger
    local _, err = pcall(getmetatable(trigger_list).__call, trigger_list,
                         nil, trigger_cnt)
    test:ok(string.find(err, "is not found"), "check error")
end)

test:test("errored trigger test", function(test)
    test:plan(6)

    --
    -- Check that trigger:run() fails on the first error
    --

    local cnt = 0
    local function trigger_cnt() cnt = cnt + 1 end
    local function trigger_errored() error("test error") end

    test:is(#trigger_list(), 0, "check for empty triggers")

    -- Append first trigger
    trigger_list(trigger_cnt)
    trigger_list:run()
    test:is(cnt, 1, "check simple trigger")
    -- Append errored trigger
    trigger_list(trigger_errored)
    pcall(function() trigger_list:run() end)
    test:is(cnt, 2, "check simple+error trigger")
    -- Flush triggers
    table_clear(trigger_list)
    test:is(#trigger_list(), 0, "successfull flush")
    -- Append first trigger
    trigger_list(trigger_errored)
    pcall(function() trigger_list:run() end)
    test:is(cnt, 2, "check error trigger")
    -- Append errored trigger
    trigger_list(trigger_cnt)
    pcall(function() trigger_list:run() end)
    test:is(cnt, 2, "check error+simple trigger")
end)

os.exit(test:check() == true and 0 or -1)
