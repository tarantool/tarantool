test_run = require('test_run').new()

--
-- gh-6027: on attempt to a commit transaction its original error was lost.
--

box.schema.user.grant('guest', 'super')
s = box.schema.create_space('test')
_ = s:create_index('pk')

test_run:cmd('create server replica with rpl_master=default, '..                \
             'script="replication/replica.lua"')
test_run:cmd('start server replica')

test_run:switch('replica')
box.error.injection.set('ERRINJ_TXN_COMMIT_ASYNC', true)

test_run:switch('default')
_ = s:replace{1}

test_run:switch('replica')
test_run:wait_upstream(1, {status = 'stopped'})
-- Should be something about error injection.
box.info.replication[1].upstream.message

test_run:switch('default')
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.error.injection.set('ERRINJ_TXN_COMMIT_ASYNC', false)
s:drop()
box.schema.user.revoke('guest', 'super')
