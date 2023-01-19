local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_interval_create_space = function()
    g.server:exec(function()
        local t = require('luatest')
        local format = {{name = "i", type = "integer"},
                        {name = "a", type = "interval"}}
        local s = box.schema.space.create('S1', {format = format})
        t.assert_equals(s ~= nil, true)
        t.assert_equals(s:format(), format)
        s:drop()
    end)
end

g.test_interval_create_index = function()
    g.server:exec(function()
        local t = require('luatest')
        local format = {{'i', 'integer'}, {'a', 'interval'}}
        local parts = {{'a', 'interval'}}
        local s = box.schema.space.create('S2', {format = format})
        local _, err = pcall(s.create_index, s, 'a', {parts = parts})
        local res = "Can't create or modify index 'a' in space 'S2': "..
                    "field type 'interval' is not supported"
        t.assert_equals(err.message, res)
        s:drop()
    end)
end

g.test_interval_insert = function()
    g.server:exec(function()
        local t = require('luatest')
        local itv = require('datetime').interval
        local val = itv.new({year = 12, day = 3, sec = 67})
        local format = {{'id', 'integer'}, {'a', 'interval'}}
        local s = box.schema.space.create('S3', {format = format})
        local _ = s:create_index('i')
        local res = s:insert({1, val})[2]
        t.assert_equals(res, val)
        s:drop()
    end)
end

g.test_interval_insert_any = function()
    g.server:exec(function()
        local t = require('luatest')
        local itv = require('datetime').interval
        local val = itv.new({year = 12, day = 3, sec = 67})
        local format = {{'id', 'integer'}, {'a', 'any'}}
        local s = box.schema.space.create('S4', {format = format})
        local _ = s:create_index('i')
        local res = s:insert({1, val})[2]
        t.assert_equals(res, val)
        s:drop()
    end)
end

g.test_interval_select = function()
    g.server:exec(function()
        local t = require('luatest')
        local itv = require('datetime').interval
        local val = itv.new({year = 12, day = 3, sec = 67})
        local format = {{'i', 'integer'}, {'a', 'interval'}}
        local s = box.schema.space.create('S5', {format = format})
        local _ = s:create_index('i')
        s:insert({1, val})
        local res1 = s:select()[1][2]
        t.assert_equals(res1, val)
        local res2 = s:select({1})[1][2]
        t.assert_equals(res2, val)
        s:drop()
    end)
end

g.test_interval_get = function()
    g.server:exec(function()
        local t = require('luatest')
        local itv = require('datetime').interval
        local val = itv.new({year = 12, day = 3, sec = 67})
        local format = {{'i', 'integer'}, {'a', 'interval'}}
        local s = box.schema.space.create('S6', {format = format})
        local _ = s:create_index('i')
        s:insert({1, val})
        local res = s:get({1})[2]
        t.assert_equals(res, val)
        s:drop()
    end)
end

g.test_interval_update = function()
    g.server:exec(function()
        local t = require('luatest')
        local itv = require('datetime').interval
        local val = itv.new({year = 12, day = 3, sec = 67})
        local format = {{'i', 'integer'}, {'a', 'interval', is_nullable = true}}
        local s = box.schema.space.create('S7', {format = format})
        local _ = s:create_index('i')
        s:insert({1, nil})
        local res = s:get({1})[2]
        t.assert_equals(res, nil)
        s:update({1}, {{'=', 2, val}})
        res = s:get({1})[2]
        t.assert_equals(res, val)
        s:drop()
    end)
end

g.test_interval_delete = function()
    g.server:exec(function()
        local t = require('luatest')
        local itv = require('datetime').interval
        local val = itv.new({year = 12, day = 3, sec = 67})
        local format = {{'i', 'integer'}, {'a', 'interval'}}
        local s = box.schema.space.create('S6', {format = format})
        local _ = s:create_index('i')
        s:insert({1, val})
        local res = s:delete({1})[2]
        t.assert_equals(res, val)
        s:drop()
    end)
end
