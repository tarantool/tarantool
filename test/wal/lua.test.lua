box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'str')
box.insert(box.schema.INDEX_ID, 0, 1, 'secondary', 'tree', 0, 1, 1, 'num')
space = box.space[0]
-- A test case for Bug#1042738
-- https://bugs.launchpad.net/tarantool/+bug/1042738
-- Iteration over a non-unique TREE index

--# setopt delimiter ';'
for i = 1, 1000 do
    space:truncate()
    for j = 1, 30 do
        space:insert(tostring(j), os.time(), 1)
    end
    count = 0
    for v in space.index[1]:iterator(box.index.ALL) do
        count = count + 1
    end
    if count ~= 30 then
        error('bug at iteration '..i..', count is '..count)
    end
end;
--# setopt delimiter ''
space:truncate()

--
-- A test case for Bug#1043858 server crash on lua stack overflow on CentOS
-- 5.4
--
for i = 1, 100000, 1 do space:insert(tostring(i),i) end
local t1 = {space:select(1)}
space:drop()


--
-- A test case for https://github.com/tarantool/tarantool/issues/65
-- Space does not exist error on repetitive access to space 0 in Lua
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

--# setopt delimiter ';'

function mktuple(n)
    local fields = { [n] = n }
    for i = 1,n do
        fields[i] = i
    end
    local t = space:replace(fields)
    assert(t[0] == 1, "tuple check")
    assert(t[n-1] == n, "tuple check")
    return string.format("count %u len %u", #t, t:bsize())
end
;

--# setopt delimiter ''

mktuple(5000)
mktuple(100000)
