env = require('test_run')
test_run = env.new()

--
-- Restart the server because need to reset quota used memory
-- after previous tests.
--

test_run:cmd('restart server default')

--
-- gh-1863 add BPS tree extents to memory quota
--

box.stat.vinyl().quota.used

space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('pk')
sec = space:create_index('sec', { parts = {2, 'unsigned'} })

space:insert({1, 1})

box.stat.vinyl().quota.used

space:insert({1, 1})

box.stat.vinyl().quota.used

space:update({1}, {{'!', 1, 100}}) -- try to modify the primary key

box.stat.vinyl().quota.used

space:insert({2, 2})
space:insert({3, 3})
space:insert({4, 4})

box.stat.vinyl().quota.used

box.snapshot()

box.stat.vinyl().quota.used

space:select{}

box.stat.vinyl().quota.used

_ = space:replace{1, 1, string.rep('a', 1024 * 1024 * 5)}

box.stat.vinyl().quota.used

space:drop()

--
-- gh-2634: check that box.cfg.vinyl_memory can be increased
--
test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd(string.format("start server test with args='%d'", 1024 * 1024))
test_run:cmd('switch test')

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

count = 20
pad = string.rep('x', 100 * 1024)

box.stat.vinyl().quota.limit

for i = 1, count do s:replace{i, pad} end -- triggers dump
box.stat.vinyl().quota.used < count * pad:len()

box.snapshot()

box.cfg{vinyl_memory = 8 * 1024 * 1024}
box.stat.vinyl().quota.limit

for i = 1, count do s:replace{i, pad} end -- does not trigger dump
box.stat.vinyl().quota.used > count * pad:len()

box.cfg{vinyl_memory = 4 * 1024 * 1024} -- error: decreasing vinyl_memory is not allowed
box.stat.vinyl().quota.limit

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
