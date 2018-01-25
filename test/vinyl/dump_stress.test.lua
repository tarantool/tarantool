test_run = require('test_run').new()

test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test with args='1048576'")
test_run:cmd('switch test')

fiber = require 'fiber'

pad_size = 1000

test_run:cmd("setopt delimiter ';'")
function gen_tuple(k)
    local pad = {}
    for i = 1,pad_size do
        pad[i] = string.char(math.random(65, 90))
    end
    return {k, k + 1, k + 2, k + 3, table.concat(pad)}
end
test_run:cmd("setopt delimiter ''");

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
_ = s:create_index('i2', {unique = false, parts = {2, 'unsigned'}})
_ = s:create_index('i3', {unique = false, parts = {3, 'unsigned'}})
_ = s:create_index('i4', {unique = false, parts = {4, 'unsigned'}})

--
-- Schedule dump caused by snapshot and memory shortage concurrently.
--
_ = fiber.create(function() while true do box.snapshot() fiber.sleep(0.01) end end)

test_run:cmd("setopt delimiter ';'")
for i = 1, 10 * box.cfg.vinyl_memory / pad_size do
    s:replace(gen_tuple(i))
    if i % 100 == 0 then
        box.commit()
        box.begin()
    end
end
test_run:cmd("setopt delimiter ''");

s.index.i1:count()
s.index.i2:count()
s.index.i3:count()
s.index.i4:count()

test_run:cmd('switch default')
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
