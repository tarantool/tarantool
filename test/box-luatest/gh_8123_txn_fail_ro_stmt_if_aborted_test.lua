local server = require('luatest.server')
local t = require('luatest')

local g_mvcc_off = t.group('gh_8123.mvcc_off')

g_mvcc_off.before_all(function(cg)
    cg.server = server:new({
        alias = 'default',
        box_cfg = {memtx_use_mvcc_engine = false},
    })
    cg.server:start()
end)

g_mvcc_off.after_all(function(cg)
    cg.server:drop()
end)

g_mvcc_off.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g_mvcc_off.test_yield = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('primary')
        box.begin()
        s:insert({1})
        fiber.yield()
        local errmsg = 'Transaction has been aborted by a fiber yield'
        t.assert_error_msg_equals(errmsg, s.get, s, {1})
        t.assert_error_msg_equals(errmsg, s.select, s, {1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {2})
        t.assert_error_msg_equals(errmsg, s.get, s, {2})
        t.assert_error_msg_equals(errmsg, s.select, s, {2})
        t.assert_error_msg_equals(errmsg, box.commit)
    end)
end

local g_mvcc_on = t.group('gh_8123.mvcc_on', t.helpers.matrix({
    engine = {'memtx', 'vinyl'},
}))

g_mvcc_on.before_all(function(cg)
    cg.server = server:new({
        alias = 'default',
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
end)

g_mvcc_on.after_all(function(cg)
    cg.server:drop()
end)

g_mvcc_on.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g_mvcc_on.test_timeout = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local s = box.schema.create_space('test', {engine = engine})
        s:create_index('primary')
        box.begin({timeout = 0.01})
        s:insert({1})
        fiber.sleep(0.1)
        local errmsg = 'Transaction has been aborted by timeout'
        t.assert_error_msg_equals(errmsg, s.get, s, {1})
        t.assert_error_msg_equals(errmsg, s.select, s, {1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {2})
        t.assert_error_msg_equals(errmsg, s.get, s, {2})
        t.assert_error_msg_equals(errmsg, s.select, s, {2})
        t.assert_error_msg_equals(errmsg, box.commit)
    end, {cg.params.engine})
end

g_mvcc_on.test_conflict = function(cg)
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        local s = box.schema.create_space('test', {engine = engine})
        s:create_index('primary')
        box.begin()
        s:insert({1})
        local f = fiber.new(s.insert, s, {1, 1})
        f:set_joinable(true)
        t.assert_equals({f:join()}, {true, {1, 1}})
        local errmsg = 'Transaction has been aborted by conflict'
        t.assert_error_msg_equals(errmsg, s.get, s, {1})
        t.assert_error_msg_equals(errmsg, s.select, s, {1})
        t.assert_error_msg_equals(errmsg, s.insert, s, {2})
        t.assert_error_msg_equals(errmsg, s.get, s, {2})
        t.assert_error_msg_equals(errmsg, s.select, s, {2})
        t.assert_error_msg_equals(errmsg, box.commit)
    end, {cg.params.engine})
end
