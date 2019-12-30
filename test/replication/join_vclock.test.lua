test_run = require('test_run').new()

test_run:cmd("create server master with script='replication/master1.lua'")
test_run:cmd('start server master')
test_run:cmd("switch master")

errinj = box.error.injection
errinj.set("ERRINJ_RELAY_FINAL_SLEEP", true)

engine = test_run:get_cfg('engine')
box.schema.user.grant('guest', 'replication')
s = box.schema.space.create('test', {engine = engine});
index = s:create_index('primary')

fiber = require('fiber')
ch = fiber.channel(1)
done = false

function repl_f() local i = 0 while not done do s:replace({i, i}) fiber.sleep(0.001) i = i + 1 end ch:put(i) end
_ = fiber.create(repl_f)

test_run:cmd("create server replica with rpl_master=master, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")

test_run:cmd("switch master")
done = true
count = ch:get()

errinj.set("ERRINJ_RELAY_FINAL_SLEEP", false)
test_run:cmd("switch replica")
test_run:cmd("setopt delimiter ';'")
-- Wait for all tuples to be inserted on replica
test_run:wait_cond(function()
    return box.space.test.index.primary:max()[1] == test_run:eval('master', 'count')[1] - 1
end);
test_run:cmd("setopt delimiter ''");
replica_count = box.space.test.index.primary:count()  master_count = test_run:eval('master', 'count')[1]
-- Verify that there are the same amount of tuples on master and replica
replica_count == master_count or {replica_count, master_count}

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster({'master', 'replica'})
