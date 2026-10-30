env = require('test_run')
test_run = env.new()
test_run:cmd("create server test with script='box/simple-tuple-size-increasing.lua'")
test_run:cmd("start server test")
test_run:cmd("switch test")

_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary')

size = 1
test_run:cmd("setopt delimiter ';'")
while size < box.cfg.memtx_max_tuple_size - 128 do
    box.space.test:insert{1, string.rep('x', size)}
    box.space.test:delete{1}
    collectgarbage('collect')
    size = size * 1.05
end;
test_run:cmd("setopt delimiter ''");

size = 1
key = 1
test_run:cmd("setopt delimiter ';'")
while size < box.cfg.memtx_max_tuple_size - 128 do
    for _ = 1, 5 do
        box.space.test:insert{key, string.rep('x', size)}
        key = key + 1
    end
    size = size * 1.05
end;
size = 1
key = 1
while size < box.cfg.memtx_max_tuple_size - 128 do
    for _ = 1, 5 do
        box.space.test:delete{1}
        key = key + 1
    end
    collectgarbage('collect')
    size = size * 1.05
end;
test_run:cmd("setopt delimiter ''");
box.space.test:drop()

test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
