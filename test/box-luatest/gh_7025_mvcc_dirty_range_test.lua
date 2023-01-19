local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary', {parts = {{1, 'uint'}, {2, 'uint'}}})
    end)
end)

g.after_each(function()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_empty_range_select_left = function()
    g.server:exec(function()
        local t = require('luatest')
        local s = box.space.test
        local key = 1
        s:replace{2, 0}
        s:replace{3, 0}

        local txn_proxy = require("test.box.lua.txn_proxy")
        local tx = txn_proxy.new()

        tx:begin()
        local strkey = tostring(key)
        local select_req = 'box.space.test:select{' .. strkey .. '}'
        local replace_req = 'box.space.test:replace{' .. strkey .. ', 1}'
        t.assert_equals(tx(select_req), {{}})
        s:replace{key, 0}
        t.assert_equals(tx(select_req), {{}})
        t.assert_equals(tx(replace_req),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(tx(select_req),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(tx:commit(),
            {{error = "Transaction has been aborted by conflict"}})

        t.assert_equals(s:select{}, {{1, 0}, {2, 0}, {3, 0}})
    end)
end

g.test_empty_range_select_middle = function()
    g.server:exec(function()
        local t = require('luatest')
        local s = box.space.test
        local key = 2
        s:replace{1, 0}
        s:replace{3, 0}

        local txn_proxy = require("test.box.lua.txn_proxy")
        local tx = txn_proxy.new()

        tx:begin()
        local strkey = tostring(key)
        local select_req = 'box.space.test:select{' .. strkey .. '}'
        local replace_req = 'box.space.test:replace{' .. strkey .. ', 1}'
        t.assert_equals(tx(select_req), {{}})
        s:replace{key, 0}
        t.assert_equals(tx(select_req), {{}})
        t.assert_equals(tx(replace_req),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(tx(select_req),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(tx:commit(),
            {{error = "Transaction has been aborted by conflict"}})

        t.assert_equals(s:select{}, {{1, 0}, {2, 0}, {3, 0}})
    end)
end

g.test_empty_range_select_right = function()
    g.server:exec(function()
        local t = require('luatest')
        local s = box.space.test
        local key = 3
        s:replace{1, 0}
        s:replace{2, 0}

        local txn_proxy = require("test.box.lua.txn_proxy")
        local tx = txn_proxy.new()

        tx:begin()
        local strkey = tostring(key)
        local select_req = 'box.space.test:select{' .. strkey .. '}'
        local replace_req = 'box.space.test:replace{' .. strkey .. ', 1}'
        t.assert_equals(tx(select_req), {{}})
        s:replace{key, 0}
        t.assert_equals(tx(select_req), {{}})
        t.assert_equals(tx(replace_req),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(tx(select_req),
                        {{error = "Transaction has been aborted by conflict"}})
        t.assert_equals(tx:commit(),
            {{error = "Transaction has been aborted by conflict"}})

        t.assert_equals(s:select{}, {{1, 0}, {2, 0}, {3, 0}})
    end)
end
