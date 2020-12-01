env = require('test_run')
test_run = env.new()

test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

engine = test_run:get_cfg('engine')

LISTEN = require('uri').parse(box.cfg.listen)

remote = require('net.box')
fiber = require('fiber')
log = require('log')

local_space = box.schema.space.create('test', { engine = engine })
pk = local_space:create_index('primary')
box.schema.user.grant('guest', 'read,write,execute', 'universe')

function local_replace(tuple) return local_space:replace(tuple) end

log.info("create connection")
conn = remote.connect(LISTEN.host, LISTEN.service)
log.info("state is %s", conn.state)
conn:ping()
log.info("ping is done")
remote_space = conn.space.test

-- Check that a transaction isn't stored in the connection, if
-- explicit conn:begin() wasn't called.
conn:eval('return 2 + 2')
remote_space:replace({1})
remote_space:select{}
-- Nothing to rollback, so the next select returns the same
-- result.
conn:rollback()
remote_space:select{}
_ = remote_space:delete({1})
remote_space:select{}

--
-- Test BEGIN of the new remote transaction.
--
conn:begin()
-- No error: the remote transaction is stored in the connection
-- object, so we can create also localhost transactions.
box.begin()
-- Error - this connection in this fiber already has the opened
-- transaction.
conn:begin()
-- Commit the local transaction, but the remote is still alive, so
-- conn:begin() returns error again.
box.commit()
conn:begin()
conn:commit()

--
-- Test attaching remote requests to the connection transaction.
--

-- Attach space:method()
conn:begin()
remote_space:replace({1})
remote_space:replace({2})
remote_space:select{} -- check result of the remote select
local_space:select{} -- result of the local select
conn:commit()

remote_space:select{}
local_space:select{}

-- Attach call/eval
conn:begin()
conn:eval('local_space:replace({1, 1})')
conn:call('local_replace', {{2, 2}})
remote_space:select{}
local_space:select{}
conn:commit()

remote_space:select{}
local_space:select{}

-- Check errors in call/eval. Error in call/eval must not rollback
-- the entire transaction.
conn:begin()
remote_space:replace({1})
conn:eval('box.box.box(123(456))')
remote_space:select{}
conn:call('error', {'1', '2', '3'})
remote_space:select{}
conn:commit()

remote_space:select{}
local_space:select{}

-- Check BEGIN via iproto and COMMIT via eval or call.
conn:begin()
_ = remote_space:delete({2})
conn:eval('box.commit()')
remote_space:select{}
-- We can start the new transaction, because the eval commited the
-- previous one.
conn:begin()
remote_space:replace({2})
conn:call('box.commit')
remote_space:select{}
-- We can start the new transaction, because the call commited the
-- previous one.
conn:begin()
conn:commit()

remote_space:select{}
local_space:select{}

function create_and_leave_opened() box.begin() local_space:replace({10}) end

-- Check rollback of the transaction, created in eval or call.
conn:eval('create_and_leave_opened()')
remote_space:select{}
local_space:select{}

conn:call('create_and_leave_opened')
remote_space:select{}
local_space:select{}

-- Check rollback on disconnect.
conn:begin()
remote_space:replace({10})

-- All transactions are aborted after the connection closed.
conn:close()
-- Close begins in net thread, so we wait until close is finished.
test_run:wait_cond(function() return not conn:is_connected() end, 1000)

local_space:select{}

local_space:drop()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
