env = require('test_run')
test_run = env.new()

test_run:cmd('create server vinyl_info with script="vinyl/vinyl_info.lua"')
test_run:cmd("start server vinyl_info")
test_run:cmd('switch vinyl_info')

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
space:replace({'xxx'})
space:get({'xxx'})
space:select()
space:delete({'xxx'})

test_run:cmd("setopt delimiter ';'")
for _, v in ipairs({ 'path', 'build', 'tx_latency', 'cursor_latency',
                     'get_latency', 'gc_active'}) do
    test_run:cmd("push filter '"..v..": .*' to '"..v..": <"..v..">'")
end;
test_run:cmd("setopt delimiter ''");
box_info_sort(box.info.vinyl())
test_run:cmd("clear filter")

space:drop()

test_run:cmd("setopt delimiter ';'")
for i = 1, 16 do
	c = box.schema.space.create('i'..i, { engine='vinyl' })
	c:create_index('pk')
end;
box_info_sort(box.info.vinyl().db);
for i = 1, 16 do
	box.space['i'..i]:drop()
end;
test_run:cmd("setopt delimiter ''");

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
old_count = box.info.vinyl().performance.write_count
space:insert({1, 1})
space:insert({2, 2})
space:insert({3, 3})
space:insert({4, 4})
box.info.vinyl().performance.write_count - old_count == 8
space:drop()

test_run:cmd('switch default')
test_run:cmd("stop server vinyl_info")
test_run:cmd("start server vinyl_info")
