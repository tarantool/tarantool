#!/usr/bin/env tarantool

local test = require('tester')

test:plan(3)

local engine = test:engine()

test:test("Simple test", function(test)
    test:plan(2)
    local space = box.schema.space.create('tester', {engine = engine})
    space:create_index('pk')
    space:before_replace(function() end)
    local status = pcall(function() space:insert({}) end)
    test:is(status, false)
    local status = pcall(function() space:insert({0}) end)
    test:is(status, true)
    space:drop{}
end)

test:test("Reproducer from issue", function(test)
    test:plan(2)
    local space = box.schema.space.create('tester', {engine = engine})
    space:create_index('primary', {
        parts = {2, 'unsigned'}
    })
    space:format({
        {name = 'x', type = 'string'},
        {name = 'y', type = 'unsigned'},
        {name = 'z', type = 'unsigned'}
    })
    space:before_replace(function() end)
    local status = pcall(function() space:insert({2}) end)
    test:is(status, false)
    local status = pcall(function() space:insert({}) end)
    test:is(status, false)
    space:drop{}
end)

test:test("Test for suitable format after every trigger", function(test)
    test:plan(1)
    local space = box.schema.space.create('tester', {engine = engine})
    space:create_index('pk', {
        parts = {1, 'unsigned'}
    })
    space:format({
        {name = 'x', type = 'unsigned'},
        {name = 'y', type = 'unsigned', is_nullable=true}
    })
    space:before_replace(function(_, new) return box.tuple.new({new[1]}) end)
    space:before_replace(function(_, new) return box.tuple.new({new[1], 'b'}) end)
    space:before_replace(function(_, new) return box.tuple.new({new[1]}) end)
    -- Despite the fact that after all before_replace trigger have been executed
    -- tuple format will match space format, after the second trigger tuple format
    -- will not be suitable so insertion must fail.
    local status = pcall(function() space:insert({1}) end)
    test:is(status, false)
    space:drop{}
end)

os.exit(test:check() and 0 or 1)
