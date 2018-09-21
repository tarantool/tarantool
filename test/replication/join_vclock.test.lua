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

replica_set.join(test_run, 1)
test_run:cmd("switch replica1")

test_run:cmd("switch default")
done = true
ch:get()

errinj.set("ERRINJ_RELAY_FINAL_SLEEP", false)
test_run:cmd("switch replica1")
cnt = box.space.test.index[0]:count()
box.space.test.index.primary:max()[1] == cnt - 1
test_run:cmd("switch default")

replica_set.drop_all(test_run)
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
