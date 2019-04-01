fiber = require('fiber')
env = require('test_run')
replica_set = require('fast_replica')
test_run = env.new()
engine = test_run:get_cfg('engine')

errinj = box.error.injection
errinj.set("ERRINJ_RELAY_FINAL_SLEEP", true)

box.schema.user.grant('guest', 'replication')
s = box.schema.space.create('test', {engine = engine});
index = s:create_index('primary')

ch = fiber.channel(1)
done = false

function repl_f() local i = 0 while not done do s:replace({i, i}) fiber.sleep(0.001) i = i + 1 end ch:put(true) end
_ = fiber.create(repl_f)

replica_set.join(test_run, "join_vclock")
test_run:cmd("switch join_vclock")

test_run:cmd("switch default")
done = true
ch:get()

errinj.set("ERRINJ_RELAY_FINAL_SLEEP", false)
test_run:cmd("switch join_vclock")
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end) or box.info.replication[1].upstream.status
test_run:wait_cond(function() return box.space.test.index.primary:max()[1] == box.space.test.index[0]:count() - 1 end) or box.space.test.index[0]:count()
test_run:cmd("switch default")

replica_set.prune(test_run, "join_vclock")
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
