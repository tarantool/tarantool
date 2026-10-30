-- gh-1088 concurrent tuple update segfaults on BITSET_ALL_NOT_SET iteration

test_run = require('test_run').new()
fiber = require('fiber')

s = box.schema.space.create('gh-1088')
_ = s:create_index('primary', {type = 'hash', parts = {1, 'num'}})
_ = s:create_index('bitset', {unique = false, type = 'BITSET', parts = {2, 'num'}})
for i = 1, 100 do s:insert{i, 0, i - 1} end

counter = 0
test_run:cmd("setopt delimiter ';'")
function update()
    for _, t in s.index.bitset:pairs(1, {iterator = box.index.BITS_ALL_NOT_SET}) do
        counter = counter + 1
        s:update(t[1], {{'+', 3, 11}})
        fiber.sleep(0)
    end
    fiber.self():cancel()
end;
test_run:cmd("setopt delimiter ''");

fibers = {}
for _ = 1, 100 do table.insert(fibers, fiber.create(update)) end

updating = true
test_run:cmd("setopt delimiter ';'")
while updating do
    updating = false
    for _, f in pairs(fibers) do
        if f:status() ~= 'dead' then updating = true end
    end
    fiber.sleep(0.001)
end;
test_run:cmd("setopt delimiter ''");

s:get(1)
s:get(2)
s:get(3)
s:get(4)

counter -- total updates counter
