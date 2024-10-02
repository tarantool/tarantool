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
-- index.count() checks fiber slice (gh-10553)
require('fiber').set_max_slice(9000)
test_run:cmd("setopt delimiter ';'")
function get_max_index_and_count()
    return box.space.test.index.primary:max()[1], box.space.test.index.primary:count()
end;
max, count = 0, 0;
for i = 1, 100 do
    max, count = box.atomic(get_max_index_and_count)
    if max ~= count - 1 then
        break
    end
end;
-- Verify that at any moment max index is corresponding to amount of tuples,
-- which means that changes apply order is correct
max == count - 1 or {max, count - 1};
test_run:cmd("setopt delimiter ''");
test_run:cmd("switch default")

replica_set.drop_all(test_run)
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
