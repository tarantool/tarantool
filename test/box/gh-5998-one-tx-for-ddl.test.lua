env = require('test_run')
test_run = env.new()
test_run:cmd("create server tx_man with script='box/tx_man.lua'")
test_run:cmd("start server tx_man")
test_run:cmd("switch tx_man")

fiber = require('fiber')
txn_proxy = require('txn_proxy')

tx1 = txn_proxy.new()
tx2 = txn_proxy.new()

s1 = box.schema.space.create('s1')
s1:create_index('pk')

for i = 0, 10 do s1:replace{i} end

-- In case of DDL operation all other transactions will be aborted.
--
_ = test_run:cmd("setopt delimiter ';'")
function create_sk()
    s1:create_index('sk')
end
_ = test_run:cmd("setopt delimiter ''");

tx1:begin()
tx2:begin()
tx1("s1:replace({13})")
tx2("s1:replace({14})")

-- Execute as a single line to avoid yields
f = fiber.new(create_sk) \
f:set_joinable(true)     \
join_result = {f:join()}
join_result
tx1:commit()
tx2:commit()

assert(box.space.s1.index[1] ~= nil)
box.space.s1:drop()

-- Original test from #5998: create two users in different TXes.
-- Now such a situation can't be achieved since if one TX yields, it will be
-- aborted.
--
_ = test_run:cmd("setopt delimiter ';'")
function create_user(name, channel)
    box.begin()
    box.space._user:select()
    channel:get()
    box.schema.user.create(name)
    box.commit()
    channel:put(true)
end
_ = test_run:cmd("setopt delimiter ''");

channel1 = fiber.channel(1)
channel2 = fiber.channel(1)

f1 = fiber.new(create_user, "internal1", channel1)
f1 = fiber.new(create_user, "internal2", channel2)
fiber.sleep(0)

channel1:put(true)
channel2:put(true)
channel1:get()

channel1:close()
channel2:close()

assert(box.schema.user.exists('internal1') == true)
assert(box.schema.user.exists('internal2') == false)

box.schema.user.drop('internal1')

-- gh-6140: segmentation fault may occur after rollback following space:drop().
--
s = box.schema.space.create('test', { engine = 'memtx' })
_ = s:create_index('primary')

txn_proxy = require('txn_proxy')
tx = txn_proxy:new()
tx:begin()
tx('s:replace{2}')
tx('s:select{}')
s:drop()
tx:rollback()

test_run:cmd("switch default")
test_run:cmd("stop server tx_man")
test_run:cmd("cleanup server tx_man")
