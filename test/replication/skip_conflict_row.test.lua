env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

box.schema.user.grant('guest', 'replication')

space = box.schema.space.create('test', {engine = engine});
index = box.space.test:create_index('primary')

test_run:cmd("create server skip_conflict_row with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server skip_conflict_row")
test_run:cmd("switch skip_conflict_row")
box.cfg{replication_skip_conflict = true}
box.space.test:insert{1}

test_run:cmd("switch default")
space:insert{1, 1}
space:insert{2}
test_run:wait_cond(function() return box.info.status == 'running' end) or box.info.status

vclock = test_run:get_vclock('default')
_ = test_run:wait_vclock("skip_conflict_row", vclock)
test_run:cmd("switch skip_conflict_row")
box.info.replication[1].upstream.message
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end) or box.info.replication[1].upstream.status
box.space.test:select()

test_run:cmd("switch default")
test_run:wait_cond(function() return box.info.status == 'running' end) or box.info.status

-- gh-2283: test that if replication_skip_conflict is off vclock
-- is not advanced on errors.
test_run:cmd("restart server skip_conflict_row")
test_run:cmd("switch skip_conflict_row")
box.space.test:insert{3}
lsn1 = box.info.vclock[1]
test_run:cmd("switch default")
box.space.test:insert{3, 3}
box.space.test:insert{4}
test_run:cmd("switch skip_conflict_row")
-- lsn is not promoted
lsn1 == box.info.vclock[1]
test_run:wait_cond(function() return box.info.replication[1].upstream.message == "Duplicate key exists in unique index 'primary' in space 'test'" end) or box.info.replication[1].upstream.message
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'stopped' end) or box.info.replication[1].upstream.status
test_run:cmd("switch default")
test_run:cmd("restart server skip_conflict_row")
-- applier is not in follow state
box.info.replication[1].upstream.message

--
-- gh-3977: check that NOP is written instead of conflicting row.
--
replication = box.cfg.replication
box.cfg{replication_skip_conflict = true, replication = {}}
box.cfg{replication = replication}
test_run:cmd("switch default")

-- test if nop were really written
box.space.test:truncate()
test_run:cmd("restart server skip_conflict_row")
test_run:cmd("switch skip_conflict_row")
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end) or box.info.replication[1].upstream.status
-- write some conflicting records on slave
for i = 1, 10 do box.space.test:insert({i, 'r'}) end
box.cfg{replication_skip_conflict = true}
v1 = box.info.vclock[1]

-- write some conflicting records on master
test_run:cmd("switch default")
for i = 1, 10 do box.space.test:insert({i, 'm'}) end

test_run:cmd("switch skip_conflict_row")
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end) or box.info.replication[1].upstream.status

-- lsn should be incremented
test_run:wait_cond(function() return v1 == box.info.vclock[1] - 10 end) or box.info.vclock[1]
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end) or box.info.replication[1].upstream.status

-- restart server and check replication continues from nop-ed vclock
test_run:cmd("switch default")
test_run:cmd("stop server skip_conflict_row")
for i = 11, 20 do box.space.test:insert({i, 'm'}) end
test_run:cmd("start server skip_conflict_row")
test_run:cmd("switch skip_conflict_row")
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end) or box.info.replication[1].upstream.status
box.space.test:select({11}, {iterator = "GE"})

test_run:cmd("switch default")
-- cleanup
test_run:cmd("stop server skip_conflict_row")
test_run:cmd("cleanup server skip_conflict_row")
test_run:cmd("delete server skip_conflict_row")
test_run:cleanup_cluster()
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
