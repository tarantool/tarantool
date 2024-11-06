local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
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
        box.internal.memtx_tx_gc(1000)
    end)
end)

-- Reproducer from gh-10448
g.test_non_unique_index_crash = function(cg)
    cg.server:exec(function()
        local space = box.schema.space.create("test", {
            format = {
                {is_nullable = false, name = "ID", type = "unsigned"},
                {is_nullable = true, name = "DATA", type = "map"},
            }
        })
        space:create_index("ID", {
            unique = true,
            type = "TREE",
            parts = {
                {field = "ID", type = "unsigned"},
            },
        })
        space:create_index("KEY", {
            unique = false,
            type = "TREE",
            parts = {
                {field = "DATA", type = "unsigned", path = "key",
                 is_nullable = false},
            },
        })

        box.begin({txn_isolation = "read-committed"})
        local key = {key = 0}
        local left_id = 1
        local left = space:insert({left_id, key})
        local right_id = 3
        local right = space:insert({right_id, key})

        t.assert_equals(
            space.index.KEY:select({}, {iterator = "EQ", after = left})[1],
            right)
        t.assert_equals(
            space.index.KEY:select({}, {iterator = "LE", after = right})[1],
            left)

        space:insert({left_id - 1, key})
        space:insert({right_id + 1, key})
        -- This insert produced crash
        space:insert({left_id + 1, key})
        box.commit()
    end)
end

-- The test checks if conflicts are collected correctly when pagination over
-- non-unique index is used and that tuples from previously iterated pages
-- are not added to readset.
g.test_non_unique_index_conflicts = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        local space = box.schema.space.create("test", {
            format = {
                {is_nullable = false, name = "k", type = "unsigned"},
                {is_nullable = false, name = "v", type = "unsigned"},
            }
        })
        space:create_index("k", {
            unique = true,
            type = "TREE",
            parts = {
                {field = "k", type = "unsigned"},
            },
        })
        space:create_index("v", {
            unique = false,
            type = "TREE",
            parts = {
                {field = "v", type = "unsigned", is_nullable = false},
            },
        })

        space:insert({1, 1})
        space:insert({5, 5})
        space:insert({10, 10})

        -- The function inserts the given tuple concurrently with pagination
        -- and returns error if the pagination transaction has conflicted
        -- with the insertion
        local function check_case(after_str, tuple_str)
            local expr = 'return box.space.test.index.v:select(nil, ' ..
                '{iterator = "GE", after = ' .. after_str .. '})'
            tx1:begin()
            tx2:begin()
            local tuples = tx1(expr)[1]
            t.assert_equals(tuples, {{10, 10}})
            tx2('box.space.test:insert' .. tuple_str)
            t.assert_equals(tx2:commit(), '')
            -- Process a write so that pagination transaction is not read-only
            -- and can actually conflict with another one
            tx1('box.space.test:replace{0, 0}')
            local err = tx1:commit()
            return type(err) == 'table' and err[1].error or nil
        end

        t.assert_equals(check_case('{5, 5}', '{3, 3}'), nil,
            "Insertion outside read page must not conflict")

        t.assert_equals(check_case('{5, 5}', '{6, 6}'),
            "Transaction has been aborted by conflict",
            "Insertion within read page must conflict")

        t.assert_equals(check_case('{8, 8}', '{7, 7}'), nil,
            "Insertion outside read page must not conflict even if pages are" ..
            "not delimited by existing tuple")
    end)
end
