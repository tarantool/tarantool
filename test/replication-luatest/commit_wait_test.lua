local t = require('luatest')
local server = require('luatest.server')

local test_matrix = {
    wait_mode = {'submit', 'none'},
    engine = {'memtx', 'vinyl'},
    memtx_use_mvcc_engine = {false, true},
}
local g = t.group('common', t.helpers.matrix(test_matrix))
table.insert(test_matrix.wait_mode, 'complete')
local g_corner_cases = t.group('corner_cases', t.helpers.matrix(test_matrix))

local function setup_server(cg)
    cg.s = server:new({
        alias = 'default',
        box_cfg = {
            memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
            replication_synchro_timeout = 120,
            replication_synchro_quorum = 2,
        },
    })
    cg.s:start()
    cg.s:exec(function(engine)
        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'are_txns_not_isolated',
               not box.cfg.memtx_use_mvcc_engine and engine ~= 'vinyl')

        box.schema.create_space('a', {engine = engine,
                                      is_sync = false}):create_index('pk')
        box.schema.create_space('s', {engine = engine,
                                      is_sync = true}):create_index('pk')
        box.ctl.promote()
    end, {cg.params.engine})
end

g.before_each(setup_server)

g.after_each(function(cg)
    cg.s:drop()
end)

-- Test that asynchronous wait modes work correctly for synchronous
-- transactions.
g.test_wait_modes_for_sync_tx = function(cg)
    cg.s:exec(function(wait_mode)
        local csw = _G.fiber.self():csw()
        box.atomic({wait = wait_mode}, function() box.space.s:replace{0} end)
        t.assert_equals(_G.fiber.self():csw(), csw)
        t.assert(box.info.synchro.queue.len, 1)

        box.begin{txn_isolation = 'read-committed'}
        t.assert_equals(box.space.s:get{0}, {0})
        box.commit()
        box.begin{txn_isolation = 'read-confirmed'}
        t.assert_equals(box.space.s:get{0},
                        _G.are_txns_not_isolated and {0} or nil)
        box.commit()

        box.cfg{replication_synchro_quorum = 1}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.s:get{0}, {0})
    end, {cg.params.wait_mode})
end

-- Test that asynchronous wait modes for correctly asynchronous transactions
-- blocked by synchronous transactions.
g.test_wait_modes_for_async_tx_after_sync_tx = function(cg)
    cg.s:exec(function(wait_mode)
        local f = _G.fiber.create(function()
            box.space.s:replace{0}
        end)
        f:set_joinable(true)
        t.assert_equals(box.info.synchro.queue.len, 1)

        local csw = _G.fiber.self():csw()
        box.atomic({wait = wait_mode}, function() box.space.a:replace{0} end)
        t.assert_equals(_G.fiber.self():csw(), csw)
        t.assert(box.info.synchro.queue.len, 2)


        box.atomic({txn_isolation = 'read-committed'}, function()
            t.assert_equals(box.space.a:get{0}, {0})
        end)
        box.atomic({txn_isolation = 'read-confirmed'}, function()
            t.assert_equals(box.space.a:get{0},
                            _G.are_txns_not_isolated and {0} or nil)
        end)

        box.cfg{replication_synchro_quorum = 1}
        t.assert(f:join())
        t.assert_equals(box.space.a:get{0}, {0})
    end, {cg.params.wait_mode})
end

g_corner_cases.before_each(setup_server)

g_corner_cases.after_each(function(cg)
    cg.s:drop()
end)

g_corner_cases.before_test('test_full_journal_queue', function(cg)
    cg.s:update_box_cfg{wal_queue_max_size = 1}
end)

-- Test that the case when the journal queue is full is handled correctly for
-- synchronous transactions using asynchronous commits modes.
g_corner_cases.test_full_journal_queue = function(cg)
    cg.s:exec(function(wait_mode)
        -- Fill up the journal queue.
        box.atomic({wait = 'none'}, function() box.space.a:replace{0} end)

        if wait_mode == 'none' then
            local msg = 'The WAL queue is full'
            t.assert_error_msg_content_equals(msg, function()
                box.atomic({wait = 'none'}, function()
                    box.space.s:replace{0}
                end)
            end)
            return
        end

        local f = _G.fiber.create(function()
            box.atomic({wait = wait_mode}, function()
                box.space.s:replace{0}
            end)
        end)
        f:set_joinable(true)
        if wait_mode == 'submit' then
            t.assert(f:join())
        end
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.atomic({txn_isolation = 'read-committed'}, function()
            t.assert_equals(box.space.s:get{0}, {0})
        end)
        box.atomic({txn_isolation = 'read-confirmed'}, function()
            t.assert_equals(box.space.s:get{0},
                            _G.are_txns_not_isolated and {0} or nil)
        end)

        box.cfg{replication_synchro_quorum = 1}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.s:get{0}, {0})
    end, {cg.params.wait_mode})
end

g_corner_cases.before_test('test_full_limbo', function(cg)
    cg.s:update_box_cfg{replication_synchro_queue_max_size = 1}
end)

-- Test that the case when the limbo is full is handled correctly for
-- synchronous transactions using asynchronous commits modes.
g_corner_cases.test_full_limbo = function(cg)
    cg.s:exec(function(wait_mode)
        -- Fill up the limbo.
        local sync_f = _G.fiber.create(function()
            box.space.s:replace{0}
        end)
        sync_f:set_joinable(true)
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_ge(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size)

        if wait_mode == 'none' then
            local msg = 'The synchronous transaction queue is full'
            t.assert_error_msg_content_equals(msg, function()
                box.atomic({wait = 'none'}, function()
                    box.space.s:replace{1}
                end)
            end)
            return
        end

        local atomic_f = _G.fiber.create(function()
            box.atomic({wait = wait_mode}, function()
                box.space.s:replace{1}
            end)
        end)
        atomic_f:set_joinable(true)
        t.assert_equals(box.info.synchro.queue.len, 1)

        box.cfg{replication_synchro_quorum = 1}
        t.assert(sync_f:join())
        box.cfg{replication_synchro_quorum = 2}
        t.assert_equals(box.space.s:get{0}, {0})
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.atomic({txn_isolation = 'read-committed'}, function()
            t.assert_equals(box.space.s:get{1}, {1})
        end)
        box.atomic({txn_isolation = 'read-confirmed'}, function()
            t.assert_equals(box.space.s:get{1},
                            _G.are_txns_not_isolated and {1} or nil)
        end)

        if wait_mode == 'complete' then
            box.cfg{replication_synchro_quorum = 1}
        end
        t.assert(atomic_f:join())
        if wait_mode == 'submit' then
            t.assert_equals(box.info.synchro.queue.len, 1)
            box.cfg{replication_synchro_quorum = 1}
        end

        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.s:get{1}, {1})
    end, {cg.params.wait_mode})
end

g_corner_cases.before_test('test_replication_synchro_timeout_with_async_commit',
                           function(cg)
    cg.s:update_box_cfg{replication_synchro_timeout = 0.001}
end)

-- Test that the timeout for synchronous transactions committing synchronously
-- after synchronous transactions using asynchronous commits modes works
-- correctly.
g_corner_cases.test_replication_synchro_timeout_with_async_commit = function(cg)
    t.skip_if(cg.params.wait_mode == 'complete')
    cg.s:exec(function(wait_mode)
        box.atomic({wait = wait_mode}, function()
            box.space.s:replace{0}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        local msg = 'Quorum collection for a synchronous transaction is ' ..
                    'timed out'
        t.assert_error_msg_content_equals(msg, function()
            box.space.s:replace{1}
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.cfg{replication_synchro_quorum = 1}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        t.assert_equals(box.space.s:get{0}, {0})
    end, {cg.params.wait_mode})
end

g_corner_cases.before_test('test_synchro_rollback_with_async_commit',
                           function(cg)
    cg.s:update_box_cfg{replication_synchro_timeout = 0.001}
end)

-- Test that synchronous transactions using asynchronous commit modes get rolled
-- back by timeout synchronous transactions.
g_corner_cases.test_synchro_rollback_with_async_commit = function(cg)
    t.skip_if(cg.params.wait_mode == 'complete')
    cg.s:exec(function(wait_mode)
        local f = _G.fiber.create(function() box.space.s:replace{0} end)
        f:set_joinable(true)
        box.atomic({wait = wait_mode}, function()
            box.space.s:replace{1}
        end)
        t.assert_equals(box.info.synchro.queue.len, 2)
        t.assert_not(f:join())
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.space.s:get{1}, nil)
    end, {cg.params.wait_mode})
end
