--
-- Check that replication applier invokes on_replace triggers
--

env = require('test_run')
test_run = env.new()
fiber = require('fiber')

_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest', 'replication')

test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
session_type = nil
--
-- gh-2642: box.session.type() in replication applier
--
_ = box.space.test:on_replace(function() session_type = box.session.type() end)
box.space.test:insert{1}
--
-- console
--
session_type
test_run:cmd("switch default")
box.space.test:insert{2}
test_run:cmd("switch replica")
while box.space.test:count() < 2 do fiber.sleep(0.01) end
--
-- applier
--
session_type
test_run:cmd("switch default")
--
-- cleanup
--
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
