local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13403-memtx-mvcc-reverse-iterator-stale-last')

g.before_all(function(cg)
    cg.server = server:new{
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

-- Check that a transaction aborted by DDL can continue a reverse iterator
-- whose last tuple was removed from the index by the abort.
g.test_aborted_txn = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('s')
        s:create_index('pk')
        local ch1 = fiber.channel()
        local ch2 = fiber.channel()
        local f = fiber.new(function()
            box.begin()
            s:replace{1}
            local gen, param, state = s:pairs({}, {iterator = 'LE'})
            local _, tuple = gen(param, state)
            ch1:put(tuple)
            ch2:get()
            -- The transaction is aborted, the tuple is not in the index.
            pcall(gen, param, state)
            box.rollback()
        end)
        f:set_joinable(true)
        t.assert_equals(ch1:get(), {1})
        s:format({{name = 'id', type = 'unsigned'}})
        ch2:put(true)
        t.assert(f:join())
    end)
end

-- Check that a reverse iterator whose last tuple was deleted and garbage
-- collected continues in a transaction and tracks the gap it has read.
g.test_deleted_last = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:insert{1}
        s:insert{3}
        local gen, param, state = s:pairs({}, {iterator = 'LE'})
        local _, tuple = gen(param, state)
        t.assert_equals(tuple, {3})
        s:delete{3}
        box.internal.memtx_tx_gc(100)
        box.begin()
        _, tuple = gen(param, state)
        t.assert_equals(tuple, {1})
        -- The read gap (1, +inf) conflicts with a concurrent insert.
        local f = require('fiber').new(function() s:insert{2} end)
        f:set_joinable(true)
        t.assert(f:join())
        t.assert_error_msg_content_equals(
            'Transaction has been aborted by conflict', s.replace, s, {4})
        box.rollback()
    end)
end
