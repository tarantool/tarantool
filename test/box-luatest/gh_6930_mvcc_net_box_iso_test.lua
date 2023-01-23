local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all = function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end

g.after_all = function()
    g.server:drop()
end

g.before_test('test_mvcc_netbox_isolation_level_basics', function()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
        box.schema.user.grant('guest', 'read,write', 'space', 'test')
    end)
end)

g.test_mvcc_netbox_isolation_level_basics = function()
    g.server:exec(function()
        local s = box.space.test
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local fiber = require('fiber')
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            s:insert{1}
        end)
        rawset(_G, 'f', f)
    end)

    local netbox = require('net.box')
    local conn = netbox.connect(g.server.net_box_uri)

    t.assert_equals(conn.space.test:select(), {})
    local strm = conn:new_stream()
    strm:begin()
    t.assert_equals(strm.space.test:select(), {})
    strm:commit()

    local expect0 = {'read-confirmed', 'best-effort',
                     box.txn_isolation_level.READ_CONFIRMED,
                     box.txn_isolation_level.BEST_EFFORT,
                     box.txn_isolation_level['read-confirmed'],
                     box.txn_isolation_level['best-effort']}

    for _,level in pairs(expect0) do
        strm:begin{txn_isolation = level}
        t.assert_equals(strm.space.test:select(), {})
        strm:commit()
    end

    for _,level in pairs(expect0) do
        g.server:exec(function(cfg_level)
            box.cfg{txn_isolation = cfg_level}
        end, {level})
        strm:begin()
        t.assert_equals(strm.space.test:select(), {})
        strm:commit()
    end
    g.server:exec(function()
        box.cfg{txn_isolation = 'best-effort'}
    end)

    local expect1 = {'read-committed',
                     box.txn_isolation_level.READ_COMMITTED,
                     box.txn_isolation_level['read-committed']}

    for _,level in pairs(expect1) do
        strm:begin{txn_isolation = level}
        t.assert_equals(strm.space.test:select(), {{1}})
        strm:commit()
    end

    for _,level in pairs(expect1) do
        g.server:exec(function(cfg_level)
            box.cfg{txn_isolation = cfg_level}
        end, {level})
        strm:begin()
        t.assert_equals(strm.space.test:select(), {{1}})
        strm:commit()
        -- txn_isolation does not affect autocommit select,
        -- which is always run as read-confirmed
        t.assert_equals(strm.space.test:select(), {})
    end
    g.server:exec(function()
        box.cfg{txn_isolation = 'best-effort'}
    end)

    -- With default best-effort isolation RO->RW transaction can be aborted:
    strm:begin()
    t.assert_equals(strm.space.test:select{1}, {})
    t.assert_error_msg_content_equals(
        "Transaction has been aborted by conflict",
        function() strm.space.test:replace{2} end)
    t.assert_error_msg_content_equals(
        "Transaction has been aborted by conflict",
        function() strm:commit() end)

    -- But using 'read-committed' allows to avoid conflict:
    strm:begin{txn_isolation = 'read-committed'}
    t.assert_equals(strm.space.test:select{1}, {{1}})
    strm.space.test:replace{2}
    g.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    strm:commit()

    t.assert_equals(strm.space.test:select{}, {{1}, {2}})

    g.server:exec(function()
        rawget(_G, 'f'):join()
    end)
end

g.after_test('test_mvcc_netbox_isolation_level_basics', function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local s = box.space.test
        if s then
            s:drop()
        end
    end)
end)
