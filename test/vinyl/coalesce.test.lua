test_run = require('test_run').new()

fiber = require('fiber')

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('primary', {unique=true, parts={1, 'unsigned'}, page_size=256, range_size=2048, run_count_per_level=1, run_size_ratio=1000})

function vyinfo() return box.space.test.index.primary:info() end

range_count = 4
tuple_size = math.ceil(vyinfo().page_size / 4)
pad_size = tuple_size - 30
assert(pad_size >= 16)
keys_per_range = math.floor(vyinfo().range_size / tuple_size)
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

-- Delete 90% of keys. Do it in two iterations, calling snapshot after
-- each of them in order to trigger compaction and actual cleanup.
test_run:cmd("setopt delimiter ';'")
for i = 1,2 do
    for k = i,key_count,2 do
        if k % 10 ~= 0 then s:delete(k) end
    end
    box.snapshot()
end;
test_run:cmd("setopt delimiter ''");

-- Wait until compaction is over (ranges being compacted can't be coalesced)
while vyinfo().range_count ~= vyinfo().run_count do fiber.sleep(0.01) end

-- Trigger range coalescing by calling compaction and wait until
-- adjacent ranges are coalesced.
while vyinfo().range_count > 1 do s:delete({1}) box.snapshot() fiber.sleep(0.01) end

vyinfo().range_count

-- Check the remaining keys.
for k = 1,key_count do v = s:get(k) assert(v == nil or v[2] == k + iter) end

s:drop()
