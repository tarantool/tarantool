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

box.info.vinyl().memory.used

space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('pk')
sec = space:create_index('sec', { parts = {2, 'unsigned'} })

space:insert({1, 1})

box.info.vinyl().memory.used

space:insert({1, 1})

box.info.vinyl().memory.used

space:update({1}, {{'!', 1, 100}}) -- try to modify the primary key

box.info.vinyl().memory.used

space:insert({2, 2})
space:insert({3, 3})
space:insert({4, 4})

box.info.vinyl().memory.used

box.snapshot()

box.info.vinyl().memory.used

space:select{}

box.info.vinyl().memory.used

_ = space:replace{1, 1, string.rep('a', 1024 * 1024 * 5)}

box.info.vinyl().memory.used

space:drop()
