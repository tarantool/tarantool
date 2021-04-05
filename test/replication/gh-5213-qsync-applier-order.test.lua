--
-- gh-5213: applier used to process CONFIRM/ROLLBACK before writing them to WAL.
-- As a result it could happen that the transactions became visible on CONFIRM,
-- then somehow weren't written to WAL, and on restart the data might not be
-- visible again. Which means rollback of confirmed data and is not acceptable
-- (on the contrary with commit after rollback).
--
test_run = require('test_run').new()
fiber = require('fiber')
old_synchro_quorum = box.cfg.replication_synchro_quorum
old_synchro_timeout = box.cfg.replication_synchro_timeout

box.schema.user.grant('guest', 'super')

s = box.schema.space.create('test', {is_sync = true})
_ = s:create_index('pk')

test_run:cmd('create server replica with rpl_master=default,\
              script="replication/gh-5213-replica.lua"')
test_run:cmd('start server replica')

fiber = require('fiber')
box.cfg{                                                                        \
    replication_synchro_quorum = 3,                                             \
    replication_synchro_timeout = 1000,                                         \
}
f = fiber.new(function() s:replace{1} end)

test_run:switch('replica')
-- Wait when the transaction is written to WAL.
test_run:wait_lsn('replica', 'default')
s = box.space.test
-- But not visible yet. Because MVCC is on, and no CONFIRM yet.
assert(s:get({1}) == nil)
-- Block the incoming CONFIRM to be able to ensure the data is not visible until
-- WAL write ends.
box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)

test_run:switch('default')
box.cfg{replication_synchro_quorum = 2}
test_run:wait_cond(function() return f:status() == 'dead' end)

test_run:switch('replica')
-- Wait when CONFIRM is in the WAL thread.
test_run:wait_cond(function()                                                   \
    return box.error.injection.get('ERRINJ_WAL_DELAY')                          \
end)
assert(s:get({1}) == nil)
box.error.injection.set('ERRINJ_WAL_DELAY', false)
-- After CONFIRM is in WAL, the transaction is committed and its data is
-- visible.
test_run:wait_cond(function() return s:get({1}) ~= nil end)

--
-- Ensure CONFIRM WAL write fail also works fine when couldn't even start a WAL
-- write. The data remains not confirmed until WAL write succeeds.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 3}
f = fiber.new(function() s:replace{2} end)

test_run:switch('replica')
test_run:wait_lsn('replica', 'default')
assert(s:get({2}) == nil)
-- Make journal write fail immediately.
box.error.injection.set('ERRINJ_WAL_IO', true)

test_run:switch('default')
box.cfg{replication_synchro_quorum = 2}
test_run:wait_cond(function() return f:status() == 'dead' end)

test_run:switch('replica')
test_run:wait_upstream(1, {status = 'stopped'})
box.error.injection.set('ERRINJ_WAL_IO', false)
assert(s:get({2}) == nil)
-- Re-subscribe.
replication = box.cfg.replication
box.cfg{replication = {}}
box.cfg{replication = replication}
test_run:wait_lsn('replica', 'default')
test_run:wait_upstream(1, {status = 'follow'})
assert(s:get({2}) ~= nil)

--
-- Ensure CONFIRM WAL write fail works fine when an error happens inside WAL
-- thread. The data remains not confirmed until WAL write succeeds.
--
test_run:switch('default')
box.cfg{replication_synchro_quorum = 3}
f = fiber.new(function() s:replace{3} end)

test_run:switch('replica')
test_run:wait_lsn('replica', 'default')
assert(s:get({3}) == nil)
-- Journal write start is going to succeed, but it will fail later on return
-- from the WAL thread.
box.error.injection.set('ERRINJ_WAL_ROTATE', true)

test_run:switch('default')
box.cfg{replication_synchro_quorum = 2}
test_run:wait_cond(function() return f:status() == 'dead' end)

test_run:switch('replica')
test_run:wait_upstream(1, {status = 'stopped'})
box.error.injection.set('ERRINJ_WAL_ROTATE', false)
assert(s:get({3}) == nil)
box.cfg{replication = {}}
box.cfg{replication = replication}
test_run:wait_lsn('replica', 'default')
test_run:wait_upstream(1, {status = 'follow'})
assert(s:get({3}) ~= nil)

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')

s:drop()
box.schema.user.revoke('guest', 'super')
box.cfg{                                                                        \
    replication_synchro_quorum = old_synchro_quorum,                            \
    replication_synchro_timeout = old_synchro_timeout,                          \
}
