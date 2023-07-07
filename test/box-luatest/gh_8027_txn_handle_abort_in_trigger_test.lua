local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'default',
        box_cfg = {memtx_use_mvcc_engine = false},
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        local s = box.space.test
        t.assert_not_equals(s, nil)
        s:before_replace(nil, nil, 't1')
        s:before_replace(nil, nil, 't2')
        s:on_replace(nil, nil, 't1')
        s:on_replace(nil, nil, 't2')
        s:drop()
    end)
end)

g.test_yield_before_replace = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('primary')
        s:before_replace(function(_, new)
            if new[1] == 10 then
                fiber.yield()
            end
            return new
        end, nil, 't1')
        s:before_replace(function(_, new)
            if new[2] == 10 then
                fiber.yield()
            end
            return new
        end, nil, 't2')
        local errmsg = 'Transaction has been aborted by a fiber yield'
        t.assert_error_msg_equals(errmsg, s.insert, s, {10, 1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {1, 10})
        box.begin()
        s:insert({1, 1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {10, 1})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(s:select(), {})
        box.begin()
        s:insert({1, 1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {2, 10})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(s:select(), {})
    end)
end

g.test_yield_on_replace = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('primary')
        s:on_replace(function(_, new)
            if new[1] == 10 then
                fiber.yield()
            end
            return new
        end)
        s:on_replace(function(_, new)
            if new[2] == 10 then
                fiber.yield()
            end
            return new
        end)
        local errmsg = 'Transaction has been aborted by a fiber yield'
        t.assert_error_msg_equals(errmsg, s.insert, s, {10, 1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {1, 10})
        box.begin()
        s:insert({1, 1})
        s:insert({10, 1})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(s:select(), {})
        box.begin()
        s:insert({1, 1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {2, 10})
        t.assert_error_msg_equals(errmsg, box.commit)
        t.assert_equals(s:select(), {})
    end)
end
