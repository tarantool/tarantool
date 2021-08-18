env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

s1 = box.schema.space.create('s1')
s2 = box.schema.space.create('s2')
_ = s1:create_index('pk')
_ = s2:create_index('pk')

for i = 0, 10 do s1:replace{i} end
for i = 0, 10 do s2:replace{i} end

errinj = box.error.injection
fiber = require('fiber')
channel1 = fiber.channel(1)
channel2 = fiber.channel(1)

-- Let's try to build two indexes in parallel. First one to finish wins
-- the race.
_ = test_run:cmd("setopt delimiter ';'")
function create_sk_in_tx(space, name, channel)
    box.begin()
    errinj.set('ERRINJ_BUILD_INDEX_DELAY', true)
    space:create_index(name)
    channel:get()
    box.commit()
    channel:put(true)
end
_ = test_run:cmd("setopt delimiter ''");

f1 = fiber.new(create_sk_in_tx, box.space.s1, "sk1", channel1)
f2 = fiber.new(create_sk_in_tx, box.space.s2, "sk2", channel2)
fiber.sleep(0)

errinj.set('ERRINJ_BUILD_INDEX_DELAY', false);
channel1:put(true)
fiber.sleep(0.01)
channel1:get()

assert(box.space.s2.index[1] == nil)
assert(box.space.s1.index[1] ~= nil)

box.space.s1:drop()
box.space.s2:drop()

channel1:close()
channel2:close()

test_run:cmd("switch default")
test_run:cmd("stop server tx_man")
test_run:cmd("cleanup server tx_man")
