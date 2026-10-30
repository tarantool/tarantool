env = require('test_run')
test_run = env.new()
test_run:cmd("create server test with script='box/simple-tuple-size-increasing.lua'")
test_run:cmd("start server test")
test_run:cmd("switch test")
digest = require('digest')

s = box.schema.space.create('test')
test_run:cmd("setopt delimiter ';'")
_ = s:create_index('primary', {
    if_not_exists = true,
    type = 'tree',
    parts = {1, 'integer'}
});
test_run:cmd("setopt delimiter ''");

size = 500 * 1024
str = digest.urandom(size)
-- insert tuples, until we get error due to no enough of memory
for i = 1, 1049 do s:insert({i, str}) end
-- truncate space, and collect garbage (free previous allocated memory)
s:truncate()
collectgarbage('collect')

-- check that we have enought space for tuples, in previous allocator strategy,
-- we get error, because spare slabs takes a lot of memory
test_run:cmd("setopt delimiter ';'")
for j = 9, 40 do
    size = j * 8 * 1024
    str = digest.urandom(size)
    for i = 1, 1024 do s:insert({i, str}) end
    for i = 1, 1024 do s:delete(i) end
    collectgarbage('collect')
end;
test_run:cmd("setopt delimiter ''");

box.space.test:drop()

test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
