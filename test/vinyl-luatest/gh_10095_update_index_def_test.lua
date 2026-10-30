local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_update_index_def = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'unsigned'}}})
        s:insert({1, 10, 100})
        t.assert_equals(s.index.sk:get({10}), {1, 10, 100})
        s.index.sk:alter({parts = {{2, 'unsigned'}, {1, 'unsigned'}}})
        t.assert_equals(s.index.sk:get({10, 1}), {1, 10, 100})
        s.index.sk:alter({parts = {{2, 'unsigned'}, {3, 'unsigned'}}})
        t.assert_equals(s.index.sk:get({10, 100}), {1, 10, 100})
    end)
end
