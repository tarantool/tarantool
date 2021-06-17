env = require('test_run')
test_run = env.new()

--
-- gh-4114. Account local space changes in a separate vclock
-- component. Do not replicate local space changes, even as NOPs.
--

box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test', {is_local=true})
_ = box.space.test:create_index("pk")

test_run:cmd('create server replica with rpl_master=default, script "replication/replica.lua"')
test_run:cmd('start server replica with wait=True, wait_load=True')

a = box.info.vclock[0] or 0
for i = 1,10 do box.space.test:insert{i} end
box.info.vclock[0] == a + 10 or box.info.vclock[0] - a

test_run:cmd('switch replica')
a = box.info.vclock[0] or 0
box.cfg{checkpoint_count=1}
box.space.test:select{}
box.space.test:insert{1}
box.snapshot()
box.space.test:insert{2}
box.snapshot()
box.space.test:insert{3}

assert(box.info.vclock[0] == a + 3)

test_run:cmd('switch default')

test_run:cmd('set variable repl_source to "replica.listen"')

box.cfg{replication=repl_source}
test_run:wait_cond(function()\
                       return box.info.replication[2].upstream and\
                       box.info.replication[2].upstream.status == 'follow'\
                   end,\
                   10)

-- Cleanup.
test_run:cmd('stop server replica')
test_run:cmd('delete server replica')
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
