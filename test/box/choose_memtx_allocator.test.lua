
-- write data recover from latest snapshot
env = require('test_run')
test_run = env.new()
test_run:cmd('create server test with script="box/choose_memtx_allocator.lua"')
--test small allocator
test_run:cmd('start server test with args="small"')
test_run:cmd('switch test')
space = box.schema.space.create('test')
space:format({ {name = 'id', type = 'unsigned'}, {name = 'year', type = 'unsigned'} })
s = space:create_index('primary', { parts = {'id'} })
for key = 1, 1000 do space:insert({key, key + 1000}) end
for key = 1, 1000 do space:replace({key, key + 5000}) end
for key = 1, 1000 do space:delete(key) end
space:drop()
test_run:cmd('switch default')
test_run:cmd('stop server test')
--test system(malloc) allocator
test_run:cmd('start server test with args="system"')
test_run:cmd('switch test')
space = box.schema.space.create('test')
space:format({ {name = 'id', type = 'unsigned'}, {name = 'year', type = 'unsigned'} })
s = space:create_index('primary', { parts = {'id'} })
for key = 1, 500000 do space:insert({key, key + 1000}) end
for key = 1, 500000 do space:replace({key, key + 5000}) end
for key = 1, 500000 do space:delete(key) end
space:drop()
test_run:cmd('switch default')
test_run:cmd('stop server test')
--test default (small) allocator
test_run:cmd('start server test')
test_run:cmd('switch test')
space = box.schema.space.create('test')
space:format({ {name = 'id', type = 'unsigned'}, {name = 'year', type = 'unsigned'} })
s = space:create_index('primary', { parts = {'id'} })
for key = 1, 1000 do space:insert({key, key + 1000}) end
for key = 1, 1000 do space:replace({key, key + 5000}) end
for key = 1, 1000 do space:delete(key) end
space:drop()
test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
test_run:cmd('delete server test')
