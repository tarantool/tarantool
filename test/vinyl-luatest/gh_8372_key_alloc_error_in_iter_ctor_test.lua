local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('primary', {parts = {1, 'string'}})
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_key_alloc_error = function(cg)
    cg.server:exec(function()
        box.cfg({vinyl_max_tuple_size = 1})
        t.assert_error_msg_matches(
            "Failed to allocate [%d]+ bytes for tuple: tuple is too large. " ..
            "Check 'vinyl_max_tuple_size' configuration option.",
            box.space.test.select, box.space.test, {'foo'})
    end)
end

g.test_pos_alloc_error = function(cg)
    cg.server:exec(function()
        box.cfg({vinyl_max_tuple_size = 100})
        t.assert_error_msg_matches(
            "Failed to allocate [%d]+ bytes for tuple: tuple is too large. " ..
            "Check 'vinyl_max_tuple_size' configuration option.",
            box.space.test.select, box.space.test, {},
            {after = {string.rep('x', 101)}})
    end)
end
