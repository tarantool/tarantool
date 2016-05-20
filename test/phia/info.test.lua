env = require('test_run')
test_run = env.new()

test_run:cmd('create server phia_info with script="phia/phia_info.lua"')
test_run:cmd("start server phia_info")
test_run:cmd('switch phia_info')

space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
space:replace({'xxx'})
space:get({'xxx'})
space:select()
space:delete({'xxx'})

test_run:cmd("setopt delimiter ';'")
for _, v in ipairs({ 'path', 'build', 'tx_latency', 'cursor_latency',
                     'get_latency'}) do
    test_run:cmd("push filter '"..v..": .*' to '"..v..": <"..v..">'")
end;
test_run:cmd("setopt delimiter ''");
box_info_sort(box.info.phia())
test_run:cmd("clear filter")

space:drop()

test_run:cmd('switch default')
test_run:cmd("stop server phia_info")
test_run:cmd("start server phia_info")
