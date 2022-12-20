local server = require('luatest.server')
local t = require('luatest')

local pg = t.group(nil, t.helpers.matrix{variant = {'gap write', 'full scan'}})

pg.before_all(function(cg)
    cg.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function(variant)
        assert(variant == 'gap write' or variant == 'full scan')
        local idx = variant == 'gap write' and 'tree' or 'hash'
        local s = box.schema.space.create('test')
        s:create_index('primary', {type = idx})
    end, {cg.params.variant})
end)

pg.after_all(function(cg)
    cg.server:drop()
end)

-- Test that checks that a read tracker that was set after gap write or full
-- scan does not get in the way after the reader is committed.
pg.test_commit_after_variant = function(cg)
    cg.server:exec(function(variant)
        assert(variant == 'gap write' or variant == 'full scan')

        local t = require('luatest')
        local txn_proxy = require('test.box.lua.txn_proxy')

        box.space.test:replace{10}

        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        tx1:begin()
        tx2:begin()

        if variant == 'gap write' then
            -- Set tx1 to track interval from minus infinity to {10}.
            tx1("box.space.test:select({}, {limit=1})")
        else
            -- Set tx1 to track full scan.
            tx1("box.space.test:select{}")
        end

        -- tx2 breaks gap or full scan, sets read tracker for tx1 and tuple {2}.
        tx2("box.space.test:replace{2}")
        -- Make tx1 to be RW.
        tx1("box.space.test:replace{1}")

        -- Commit both in certain order.
        t.assert_equals(tx1:commit(), "")
        t.assert_equals(tx2:commit(), "")

        local selection = box.space.test:select{}
        table.sort(selection, function(a, b) return a[1] < b[1] end)
        t.assert_equals(selection, {{1}, {2}, {10}})
    end, {cg.params.variant})
end
