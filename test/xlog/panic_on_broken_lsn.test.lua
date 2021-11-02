-- Issue 3105: Test logging of request with broken lsn before panicking
-- Two cases are covered: Recovery and Joining a new replica
env = require('test_run')
test_run = env.new()
test_run:cleanup_cluster()

fio = require('fio')

-- Testing case of panic on recovery
test_run:cmd('create server panic with script="xlog/panic.lua"')
test_run:cmd('start server panic')
test_run:switch('panic')

box.space._schema:replace{"t0", "v0"}
lsn = box.info.vclock[1]
box.error.injection.set("ERRINJ_WAL_BREAK_LSN", lsn + 1)
box.space._schema:replace{"t0", "v1"}
box.error.injection.set("ERRINJ_WAL_BREAK_LSN", -1)

test_run:switch('default')
test_run:cmd('stop server panic')

dirname = fio.pathjoin(fio.cwd(), "panic")
xlogs = fio.glob(dirname .. "/*.xlog")
fio.unlink(xlogs[#xlogs])

test_run:cmd('start server panic with crash_expected=True')

-- Check that log contains the mention of broken LSN and the request printout
filename = fio.pathjoin(fio.cwd(), 'panic.log')
str = string.format("LSN for 1 is used twice or COMMIT order is broken: confirmed: 1, new: 1, req: .*")
found = test_run:grep_log(nil, str, 256, {filename = filename})
(found:gsub('^.*, req: ', ''):gsub('lsn: %d+', 'lsn: <lsn>'))

test_run:cmd('cleanup server panic')
test_run:cmd('delete server panic')

-- Testing case of panic on joining a new replica
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test', {id = 9000})
_ = box.space.test:create_index('pk')
box.space.test:auto_increment{'v0'}

-- Inject a broken LSN in the final join stage.
lsn = -1
box.error.injection.set("ERRINJ_REPLICA_JOIN_DELAY", true)

fiber = require('fiber')
-- Asynchronously run a function that will:
-- 1. Wait for the replica join to start.
-- 2. Make sure that the record about the new replica written to
--    the _cluster space hits the WAL by writing a row to the test
--    space. This is important, because at the next step we need
--    to compute the LSN of the row that is going to be written to
--    the WAL next so we don't want to race with in-progress WAL
--    writes.
-- 3. Inject an error into replication of the next WAL row and write
--    a row to the test space. This row should break replication.
-- 4. Resume the replica join.
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    test_run:wait_cond(function() return box.info.replication[2] ~= nil end)
    box.space.test:auto_increment{'v1'}
    lsn = box.info.vclock[1]
    box.error.injection.set("ERRINJ_RELAY_BREAK_LSN", lsn + 1)
    box.space.test:auto_increment{'v2'}
    box.error.injection.set("ERRINJ_REPLICA_JOIN_DELAY", false)
end);
test_run:cmd("setopt delimiter ''");

test_run:cmd('create server replica with rpl_master=default, script="xlog/replica.lua"')
test_run:cmd('start server replica with crash_expected=True')
box.error.injection.set("ERRINJ_RELAY_BREAK_LSN", -1)

-- Check that log contains the mention of broken LSN and the request printout
filename = fio.pathjoin(fio.cwd(), 'replica.log')
str = string.format("LSN for 1 is used twice or COMMIT order is broken: confirmed: %d, new: %d, req: .*", lsn, lsn)
found = test_run:grep_log(nil, str, 256, {filename = filename})
(found:gsub('^.*, req: ', ''):gsub('lsn: %d+', 'lsn: <lsn>'))

test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')

box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
