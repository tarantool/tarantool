env = require('test_run')
test_run = env.new()
space = box.schema.space.create('tweedledum')
index1 = space:create_index('primary', { type ='hash', parts = {1, 'string'}, unique = true })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'unsigned'}, unique = false })
-- A test case for Bug#1042738
-- https://bugs.launchpad.net/tarantool/+bug/1042738
-- Iteration over a non-unique TREE index

test_run:cmd("setopt delimiter ';'")
for i = 1, 1000 do
    space:truncate()
    for j = 1, 30 do
        space:insert{tostring(j), os.time(), 1}
    end
    count = 0
    for state, v in space.index[1]:pairs() do
        count = count + 1
    end
    if count ~= 30 then
        error('bug at iteration '..i..', count is '..count)
    end
end;
test_run:cmd("setopt delimiter ''");
space:truncate()

--
-- A test case for Bug#1043858 server crash on lua stack overflow on CentOS
-- 5.4
--
for i = 1, 100000, 1 do space:insert{tostring(i), i} end
local t1 = space.index['secondary']:select()
space:drop()

--
-- A test case for https://github.com/tarantool/tarantool/issues/65
-- Space does not exist error on repetitive access to space 0 in Lua
--
space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

test_run:cmd("setopt delimiter ';'")
function mktuple(n)
    local fields = { [n] = n }
    for i = 1,n do
        fields[i] = i
    end
    local t = space:replace(fields)
    assert(t[1] == 1, "tuple check")
    assert(t[n] == n, "tuple check")
    return string.format("count %u len %u", #t, t:bsize())
end;
test_run:cmd("setopt delimiter ''");

mktuple(5000)
mktuple(100000)
space:drop()

-- https://github.com/tarantool/tarantool/issues/1323
-- index:count() works too long
fiber = require('fiber')
s = box.schema.create_space('test')
i1 = s:create_index('test', {parts = {1, 'unsigned'}})
for i = 1,10000 do s:insert{i} end
count = 0
done = false
function test1() for i = 1,100 do count = count + i1:count() end end
function test2() for j = 1,100 do test1() fiber.sleep(0) end done = true  end
fib = fiber.create(test2)
for i = 1,100 do if done then break end fiber.sleep(0.01) end
done and "count was calculated fast enough" or "count took too long to calculate"
count
box.space.test:drop()
