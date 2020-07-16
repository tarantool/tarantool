#!/usr/bin/env tarantool

--
-- gh-3159: test on_schema_init trigger
--
local tap = require('tap')
local test = tap.test('on_schema_init')
local str = ''
test:plan(7)

local function testing_trig()
    test:istable(box.space._space, 'system spaces are accessible')
    test:is(type(box.space._space.before_replace), 'function', 'before_replace triggers')
    test:is(type(box.space._space.on_replace), 'function', 'on_replace triggers')
    test:is(type(box.space._space:on_replace(function() str = str.."_space:on_replace" end)),
            'function', 'set on_replace trigger')
    str = str..'on_schema_init'
end

local trig = box.ctl.on_schema_init(testing_trig)
test:is(type(trig), 'function', 'on_schema_init trigger set')

box.cfg{log = 'tarantool.log'}
test:like(str, 'on_schema_init', 'on_schema_init trigger works')
str = ''
box.schema.space.create("test")
-- test that _space.on_replace trigger may be set in on_schema_init
test:like(str, '_space:on_replace', 'can set on_replace')
test:check()
box.space.test:drop()
os.exit(0)
