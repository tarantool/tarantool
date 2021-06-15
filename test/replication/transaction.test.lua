env = require('test_run')
test_run = env.new()
box.schema.user.grant('guest', 'replication')
engine = test_run:get_cfg('engine')

s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
l = box.schema.space.create('l_space', {engine = engine, is_local = true})
_ = l:create_index('pk')

-- transaction w/o conflict
box.begin() s:insert({1, 'm'}) s:insert({2, 'm'}) box.commit()

test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")

-- insert a conflicting row
box.space.test:replace({4, 'r'})
v1 = box.info.vclock

test_run:cmd("switch default")
-- create a two-row transaction with conflicting second
box.begin() s:insert({3, 'm'}) s:insert({4, 'm'}) box.commit()
-- create a third transaction
box.begin() s:insert({5, 'm'}) s:insert({6, 'm'}) s:insert({7, 'm'}) box.commit()

test_run:cmd("switch replica")
-- nothing was applied
v1[1] == box.info.vclock[1]
box.space.test:select()
-- check replication status
box.info.replication[1].upstream.status
box.info.replication[1].upstream.message
-- set conflict to third transaction
_ = box.space.test:delete({4})
box.space.test:replace({6, 'r'})
-- restart replication
replication = box.cfg.replication
box.cfg{replication = {}}
box.cfg{replication = replication}
-- replication stopped of third transaction
-- flush wal
box.space.l_space:replace({1})
v1[1] + 2 == box.info.vclock[1]
box.space.test:select()
-- check replication status
box.info.replication[1].upstream.status
box.info.replication[1].upstream.message

-- check restart does not help
test_run:cmd("switch default")
test_run:cmd("restart server replica")
test_run:cmd("switch replica")

box.space.test:select()
-- set skip conflict rows and check that non-conflicting were applied
replication = box.cfg.replication
box.cfg{replication = {}, replication_skip_conflict = true}
box.cfg{replication = replication}

-- check last transaction applied without conflicting row
box.space.test:select()
box.info.replication[1].upstream.status

-- make some new conflicting rows with skip-conflicts
box.space.test:replace({8, 'r'})
box.space.test:replace({9, 'r'})

-- issue a conflicting tx
test_run:cmd("switch default")
box.begin() s:insert({8, 'm'}) s:insert({9, 'm'}) box.commit()

test_run:cmd("switch replica")
-- vclock should be increased but rows skipped
box.space.test:select()

-- check restart does not change something
test_run:cmd("switch default")
test_run:cmd("restart server replica")
test_run:cmd("switch replica")

box.space.test:select()
test_run:wait_upstream(1, {status = 'follow'})

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

box.schema.user.revoke('guest', 'replication')
s:drop()
