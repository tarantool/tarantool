local server = require('luatest.server')
local t = require('luatest')

local pg = t.group(nil, t.helpers.matrix({engine = {'memtx', 'vinyl'},
                                          idx = {0, 1}}))

pg.before_all(function(cg)
    cg.server = server:new{
        alias   = 'default',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
end)

pg.after_all(function(cg)
    cg.server:drop()
end)

pg.before_each(function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.create_space('test', {engine = engine})
        s:create_index('pk')
        s:create_index('sk', {parts = {{2, 'uint'}}})
    end, {cg.params.engine})
end)

pg.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Check that if transaction with insertion is prepared and then aborted,
-- its readers are aborted too.
pg.test_abort_readers_of_insertion = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(idx)
        local txn_proxy = require('test.box.lua.txn_proxy')
        local fiber = require('fiber')

        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        tx1:begin()
        tx2:begin()
        tx1('box.space.test:replace{1, 1}')
        tx2('box.space.test:replace{2, 2}')
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        local fib = fiber.create(tx1.commit, tx1)
        fib:set_joinable(true)
        -- select by any index must read {1, 1} and lead to the same behavior.
        t.assert_equals(tx2('box.space.test.index[' .. idx ..']:select{1}'),
                        {{{1, 1}}})
        fib:join()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        t.assert_equals(tx2:commit(),
                        {{error = "Transaction has been aborted by conflict"}})
    end, {cg.params.idx})
end

-- Check that if transaction with deletion is prepared and then aborted,
-- its readers are aborted too.
pg.test_abort_readers_of_deletion = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(idx)
        local txn_proxy = require('test.box.lua.txn_proxy')
        local fiber = require('fiber')

        box.space.test:replace{1, 1}
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()
        tx1:begin()
        tx2:begin()
        tx1('box.space.test:delete{1}')
        tx2('box.space.test:replace{2, 2}')
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        local fib = fiber.create(tx1.commit, tx1)
        fib:set_joinable(true)
        -- select by any index must read {} and lead to the same behavior.
        t.assert_equals(tx2('box.space.test.index[' .. idx ..']:select{1}'),
                        {{}})
        fib:join()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        t.assert_equals(tx2:commit(),
                        {{error = "Transaction has been aborted by conflict"}})
    end, {cg.params.idx})
end
