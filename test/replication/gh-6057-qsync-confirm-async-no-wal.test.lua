test_run = require('test_run').new()
fiber = require('fiber')

--
-- gh-6057: while CONFIRM is being written, there might appear async
-- transactions not having an LSN/signature yet. When CONFIRM is done, it covers
-- these transactions too since they don't need to wait for an ACK, but it
-- should not try to complete them. Because their WAL write is not done and
-- it might even fail later. It should simply turn them into plain transactions
-- not depending on any synchronous ones.
--

old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout
box.cfg{                                                                        \
    replication_synchro_quorum = 1,                                             \
    replication_synchro_timeout = 1000000,                                      \
}
s = box.schema.create_space('test', {is_sync = true})
_ = s:create_index('pk')
s2 = box.schema.create_space('test2')
_ = s2:create_index('pk')

box.ctl.promote()

errinj = box.error.injection

function create_hanging_async_after_confirm(sync_key, async_key1, async_key2)   \
-- Let the transaction itself to WAL, but CONFIRM will be blocked.              \
    errinj.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)                                 \
    local lsn = box.info.lsn                                                    \
    local f = fiber.create(function() s:replace{sync_key} end)                  \
    test_run:wait_cond(function() return box.info.lsn == lsn + 1 end)           \
-- Wait for the CONFIRM block. WAL thread is in busy loop now.                  \
    test_run:wait_cond(function() return errinj.get('ERRINJ_WAL_DELAY') end)    \
                                                                                \
-- Create 2 async transactions to ensure multiple of them are handled fine.     \
-- But return only fiber of the second one. It is enough because if it is       \
-- finished, the first one is too.                                              \
    fiber.new(function() s2:replace{async_key1} end)                            \
    local f2 = fiber.new(function() s2:replace{async_key2} end)                 \
    fiber.yield()                                                               \
-- When WAL thread would finish CONFIRM, it should be blocked on the async      \
-- transaction so as it wouldn't be completed when CONFIRM is processed.        \
    errinj.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)                                 \
-- Let CONFIRM go.                                                              \
    errinj.set('ERRINJ_WAL_DELAY', false)                                       \
-- And WAL thread should be blocked on the async txn.                           \
    test_run:wait_cond(function() return errinj.get('ERRINJ_WAL_DELAY') end)    \
-- Wait CONFIRM finish.                                                         \
    test_run:wait_cond(function() return f:status() == 'dead' end)              \
    return f2                                                                   \
end

async_f = create_hanging_async_after_confirm(1, 2, 3)
-- Let the async transaction finish its WAL write.
errinj.set('ERRINJ_WAL_DELAY', false)
-- It should see that even though it is in the limbo, it does not have any
-- synchronous transactions to wait for and can complete right away.
test_run:wait_cond(function() return async_f:status() == 'dead' end)

assert(s:get({1}) ~= nil)
assert(s2:get({2}) ~= nil)
assert(s2:get({3}) ~= nil)

--
-- Try all the same, but the async transaction fails its WAL write.
--
async_f = create_hanging_async_after_confirm(4, 5, 6)
-- The async transaction will fail to go to WAL when WAL thread is unblocked.
errinj.set('ERRINJ_WAL_ROTATE', true)
errinj.set('ERRINJ_WAL_DELAY', false)
test_run:wait_cond(function() return async_f:status() == 'dead' end)
errinj.set('ERRINJ_WAL_ROTATE', false)

assert(s:get({4}) ~= nil)
assert(s2:get({5}) == nil)
assert(s2:get({6}) == nil)

-- Ensure nothing is broken, works fine.
s:replace{7}
s2:replace{8}

s:drop()
s2:drop()

box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
}
box.ctl.demote()
