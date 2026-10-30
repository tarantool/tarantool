test_run = require('test_run').new()

fiber = require('fiber')

-- Temporary table to restore variables after restart.
var = box.schema.space.create('var')
_ = var:create_index('primary', {parts = {1, 'string'}})

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('primary', {unique=true, parts={1, 'unsigned'}, page_size=256, range_size=2048, run_count_per_level=1, run_size_ratio=1000})

function vyinfo() return box.space.test.index.primary:stat() end

range_count = 4
tuple_size = math.ceil(s.index.primary.options.page_size / 4)
pad_size = tuple_size - 30
assert(pad_size >= 16)
keys_per_range = math.floor(s.index.primary.options.range_size / tuple_size)
key_count = range_count * keys_per_range

-- Rewrite the space until enough ranges are created.
test_run:cmd("setopt delimiter ';'")
iter = 0
function gen_tuple(k)
    local pad = {}
    for i = 1,pad_size do
        pad[i] = string.char(math.random(65, 90))
    end
    return {k, k + iter, table.concat(pad)}
end
while vyinfo().range_count < range_count do
    iter = iter + 1
    for k = key_count,1,-1 do s:replace(gen_tuple(k)) end
    box.snapshot()
    fiber.sleep(0.01)
end;
test_run:cmd("setopt delimiter ''");

vyinfo().range_count

-- Remember the number of iterations and the number of keys
-- so that we can check data validity after restart.
_ = var:insert{'iter', iter}
_ = var:insert{'key_count', key_count}
_ = var:insert{'keys_per_range', keys_per_range}

-- Check that the space can be recovered after splitting ranges.
test_run:cmd('restart server default')

fiber = require 'fiber'

s = box.space.test
var = box.space.var

iter = var:get('iter')[2]
key_count = var:get('key_count')[2]
keys_per_range = var:get('keys_per_range')[2]

function vyinfo() return box.space.test.index.primary:stat() end

-- Check the space content.
s:count() == key_count
for k = 1,key_count do v = s:get(k) assert(v[2] == k + iter) end

-- Delete 90% keys, remove padding for the rest.
test_run:cmd("setopt delimiter ';'")
for k = 1,key_count do
    if k % 10 ~= 0 then
        s:delete(k)
    else
        s:update(k, {{'#', 3, 1}})
    end
end
test_run:cmd("setopt delimiter ''");
box.snapshot()

-- Trigger compaction until ranges are coalesced.
test_run:cmd("setopt delimiter ';'")
while vyinfo().range_count > 1 do
    for i = 1,key_count,keys_per_range do
        s:delete{i}
    end
    box.snapshot()
    fiber.sleep(0.01)
end
test_run:cmd("setopt delimiter ''");

vyinfo().range_count

-- Check that the space can be recovered after coalescing ranges.
test_run:cmd('restart server default')

s = box.space.test
var = box.space.var

iter = var:get('iter')[2]
key_count = var:get('key_count')[2]

-- Check the space content.
test_run:cmd("setopt delimiter ';'")
key_count_left = 0
for k = 1,key_count do
    v = s:get(k)
    if k % 10 == 0 then
        assert(v[2] == k + iter)
        key_count_left = key_count_left + 1
    else
        assert(v == nil)
    end
end
test_run:cmd("setopt delimiter ''");
s:count() == key_count_left

s:drop()
var:drop()
